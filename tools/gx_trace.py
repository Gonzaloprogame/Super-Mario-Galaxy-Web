#!/usr/bin/env python3
"""Walks a GX trace command by command, as the console's command processor reads the stream.

The bridge (web/host/aurora_bridge.cpp) prints the trace in the engine's log when Aurora stops on an error: lines
"[gx-trace] begin", base64, "[gx-trace] end". Its records: 'S', u32 length, the bytes of a span of the game's command
stream; 'M', u8 finish, u16 token, a PE marker. This reads the commands with the hardware's sizes (vertex size from
the VCD and VAT registers the stream has set), independently of Aurora, and stops at the first byte that is not a
command: where a reader of the stream would lose its place.

    python3 -I tools/gx_trace.py <log or trace file> [--all]   (--all: print every command, not just the last ones)
"""
import base64
import struct
import sys

BP_NAMES = {0x45: "PE_DONE (draw done)", 0x47: "PE_TOKEN", 0x48: "PE_TOKEN_INT", 0x49: "EFB_SRC_TL", 0x4A: "EFB_SRC_WH",
            0x4B: "EFB_DST_BASE", 0x4F: "CLEAR_AR", 0x50: "CLEAR_GB", 0x51: "CLEAR_Z", 0x52: "COPY_EXECUTE",
            0x64: "LOADTLUT0", 0x65: "LOADTLUT1", 0x66: "TX_INVALTAGS", 0xFE: "BP_MASK"}
PRIMS = {0x80: "QUADS", 0x88: "QUADS2", 0x90: "TRIANGLES", 0x98: "TRIANGLE_STRIP", 0xA0: "TRIANGLE_FAN", 0xA8: "LINES",
         0xB0: "LINE_STRIP", 0xB8: "POINTS"}
COMPONENT = {0: 1, 1: 1, 2: 2, 3: 2, 4: 4}  # U8, S8, U16, S16, F32
COLOUR = {0: 2, 1: 3, 2: 4, 3: 2, 4: 3, 5: 4}  # RGB565, RGB8, RGBX8, RGBA4, RGBA6, RGBA8
ATTRS = ["PNMTXIDX"] + [f"TEX{i}MTXIDX" for i in range(8)] + ["POS", "NRM", "CLR0", "CLR1"] + [f"TEX{i}" for i in range(8)]


def read_trace(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    if "[gx-trace] begin" in text:
        lines = text.split("[gx-trace] begin", 1)[1].split("[gx-trace] end", 1)[0].splitlines()[1:]
        payload = "".join(line.replace("[gx-trace]", "").strip() for line in lines)
        return base64.b64decode(payload)
    return open(path, "rb").read()


def records(trace):
    at = 0
    while at < len(trace):
        kind = chr(trace[at])
        if kind == "S":
            (size,) = struct.unpack_from(">I", trace, at + 1)
            yield "S", trace[at + 5:at + 5 + size]
            at += 5 + size
        elif kind == "M":
            yield "M", (trace[at + 1], struct.unpack_from(">H", trace, at + 2)[0])
            at += 4
        else:
            raise ValueError(f"trace record {kind!r} at {at}: not a GX trace")


class State:
    def __init__(self):
        self.vcd = [0] * 21  # per attribute: 0 none, 1 direct, 2 index8, 3 index16
        self.vat = [[0, 0, 0] for _ in range(8)]

    def set_cp(self, addr, value):
        if addr == 0x50:
            for i in range(9):
                self.vcd[i] = value >> i & 1
            for k, attr in enumerate((9, 10, 11, 12)):
                self.vcd[attr] = value >> (9 + 2 * k) & 3
        elif addr == 0x60:
            for i in range(8):
                self.vcd[13 + i] = value >> (2 * i) & 3
        elif 0x70 <= addr < 0x78:
            self.vat[addr - 0x70][0] = value
        elif 0x80 <= addr < 0x88:
            self.vat[addr - 0x80][1] = value
        elif 0x90 <= addr < 0x98:
            self.vat[addr - 0x90][2] = value

    def formats(self, fmt):
        a, b, c = self.vat[fmt]
        tex = [(a >> 21 & 1, a >> 22 & 7), (b & 1, b >> 1 & 7), (b >> 9 & 1, b >> 10 & 7), (b >> 18 & 1, b >> 19 & 7),
               (b >> 27 & 1, b >> 28 & 7), (c >> 5 & 1, c >> 6 & 7), (c >> 14 & 1, c >> 15 & 7), (c >> 23 & 1, c >> 24 & 7)]
        return {"pos": (a & 1, a >> 1 & 7), "nrm": (a >> 9 & 1, a >> 10 & 7, a >> 31 & 1),
                "clr": [(a >> 13 & 1, a >> 14 & 7), (a >> 17 & 1, a >> 18 & 7)], "tex": tex}

    def vertex_size(self, fmt):
        f = self.formats(fmt)
        size = 0
        parts = []
        for attr in range(21):
            desc = self.vcd[attr]
            if desc == 0:
                continue
            if attr < 9:
                n = 1
            elif desc >= 2:
                index = 1 if desc == 2 else 2
                n = index * 3 if attr == 10 and f["nrm"][0] == 1 and f["nrm"][2] == 1 else index
            elif attr == 9:
                n = COMPONENT.get(f["pos"][1], 0) * (3 if f["pos"][0] else 2)
            elif attr == 10:
                n = COMPONENT.get(f["nrm"][1], 0) * (9 if f["nrm"][0] else 3)
            elif attr in (11, 12):
                n = COLOUR.get(f["clr"][attr - 11][1], 0)
            else:
                cnt, kind = f["tex"][attr - 13]
                n = COMPONENT.get(kind, 0) * (2 if cnt else 1)
            size += n
            parts.append(f"{ATTRS[attr]}:{['-', 'direct', 'idx8', 'idx16'][desc]}={n}")
        return size, parts


def walk(stream, show_all):
    state = State()
    log = []
    at = 0
    while at < len(stream):
        op = stream[at]
        start = at
        if op == 0x00:
            text, at = "NOP", at + 1
        elif op == 0x61:
            (value,) = struct.unpack_from(">I", stream, at + 1)
            reg = value >> 24
            text, at = f"BP {reg:02X} {BP_NAMES.get(reg, '')} = {value & 0xFFFFFF:06X}", at + 5
        elif op == 0x08:
            addr = stream[at + 1]
            (value,) = struct.unpack_from(">I", stream, at + 2)
            state.set_cp(addr, value)
            text, at = f"CP {addr:02X} = {value:08X}", at + 6
        elif op == 0x10:
            (header,) = struct.unpack_from(">I", stream, at + 1)
            count = (header >> 16) + 1
            text, at = f"XF {header & 0xFFFF:04X} x{count}", at + 5 + 4 * count
        elif op in (0x20, 0x28, 0x30, 0x38):
            text, at = f"LOAD_INDX {'ABCD'[(op - 0x20) // 8]}", at + 5
        elif op == 0x40:
            address, size = struct.unpack_from(">II", stream, at + 1)
            text, at = f"CALL_DL 0x{address:08X} {size} bytes", at + 9
        elif op == 0x48:
            text, at = "INVAL_VTX", at + 1
        elif 0x80 <= op < 0xC0:
            prim, fmt = op & 0xF8, op & 7
            (count,) = struct.unpack_from(">H", stream, at + 1)
            size, parts = state.vertex_size(fmt)
            text = f"DRAW {PRIMS[prim]} fmt{fmt} {count} vertices x {size} bytes [{', '.join(parts)}]"
            at += 3 + count * size
        else:
            log.append((start, f"!! 0x{op:02X} is not a GX command: the stream's reader lost its place before here"))
            context = stream[max(0, start - 24):start + 24]
            log.append((start, "bytes around it: " + " ".join(f"{b:02x}" for b in context)))
            return log, False
        log.append((start, text))
        if show_all:
            print(f"{start:6d}  {text}")
    return log, True


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    show_all = "--all" in sys.argv
    trace = read_trace(sys.argv[1])
    stream = bytearray()
    markers = 0
    for kind, value in records(trace):
        if kind == "S":
            stream += value
        else:
            markers += 1
    print(f"trace: {len(trace)} bytes, GX stream {len(stream)} bytes, {markers} PE markers")
    log, complete = walk(bytes(stream), show_all)
    if not show_all:
        for offset, text in log[-40:]:
            print(f"{offset:6d}  {text}")
    print("the whole stream is GX commands" if complete else "the walk stopped: see the last commands above")
    sys.exit(0 if complete else 1)


if __name__ == "__main__":
    main()
