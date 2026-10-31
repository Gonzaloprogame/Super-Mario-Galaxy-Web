#!/usr/bin/env python3
"""Names the wasm functions of an error's stack in a log copied from the engine's page.

The page logs the stack of an abort in the engine ("[página] pila del error:"), with lines like
"at .../smg_web.wasm:wasm-function[685]:0x37e8b". The build writes, next to smg_web.wasm, smg_web.js.symbols
("index:name" per line, --emit-symbol-map); this prints each frame with its name. The symbols must come from the same
build as the page's smg_web.wasm.

    python3 -I tools/wasm_symbols.py <copied log> [build/all/engine/smg_web.js.symbols]
"""
import re
import sys


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    symbols_path = sys.argv[2] if len(sys.argv) > 2 else "build/all/engine/smg_web.js.symbols"
    names = {}
    with open(symbols_path, encoding="utf-8", errors="replace") as symbols:
        for line in symbols:
            index, _, name = line.rstrip("\n").partition(":")
            if index.isdigit():
                names[int(index)] = name
    frames = 0
    with open(sys.argv[1], encoding="utf-8", errors="replace") as log:
        for line in log:
            match = re.search(r"wasm-function\[(\d+)\]", line)
            if match:
                index = int(match.group(1))
                print(f"{index:6d}  {names.get(index, '(not in the symbols: another build?)')}")
                frames += 1
    if frames == 0:
        print("no wasm-function[...] frames in the log")


if __name__ == "__main__":
    main()
