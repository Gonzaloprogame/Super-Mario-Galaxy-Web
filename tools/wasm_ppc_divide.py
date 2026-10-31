#!/usr/bin/env python3
"""PowerPC's integer division in a linked WebAssembly module.

On the console divw/divwu never trap: a zero divisor, or divw's most negative dividend by -1, gives a quotient of 0
(-1 for a negative dividend with divw) and the game goes on with it (Nebula: FlipPanel::endClipped divides by a frame
count that is 0 until its animation starts). WebAssembly traps in exactly those cases, ending the program. Nebula
finishes the trap natively with the console's result (native/sdk/os/divide_native.c); wasm cannot resume after a
trap, so this rewrites the module instead: every i32/i64 div_s, div_u, rem_s and rem_u becomes a call to a helper
that gives Nebula's results in the trapping cases and divides normally otherwise:

    signed:    q = a < 0 ? -1 : 0       r = a - q * b   (wrapping)
    unsigned:  q = 0                    r = a

Binaryen has no pass for this (its trap-mode passes are gone), so the module goes through Binaryen's text form:
wasm-dis, the instructions renamed to calls, the helpers appended, wasm-as. Emscripten strips the target features
section from its output, so the features to assemble with are given (--features, Binaryen's --enable-* names).

    python3 -I tools/wasm_ppc_divide.py <in.wasm> <out.wasm> [--features threads,bulk-memory,...] [--bin DIR]
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

# The features Emscripten 6.0.13 enables by default, plus threads for -pthread builds.
DEFAULT_FEATURES = ("threads,bulk-memory,bulk-memory-opt,mutable-globals,sign-ext,nontrapping-float-to-int,"
                    "reference-types,multivalue,call-indirect-overlong")

OPS = {
    "i32.div_s": "$__nebula_ppc_div_s_i32",
    "i32.div_u": "$__nebula_ppc_div_u_i32",
    "i32.rem_s": "$__nebula_ppc_rem_s_i32",
    "i32.rem_u": "$__nebula_ppc_rem_u_i32",
    "i64.div_s": "$__nebula_ppc_div_s_i64",
    "i64.div_u": "$__nebula_ppc_div_u_i64",
    "i64.rem_s": "$__nebula_ppc_rem_s_i64",
    "i64.rem_u": "$__nebula_ppc_rem_u_i64",
}


def helpers() -> str:
    out = []
    for t, bits in (("i32", 32), ("i64", 64)):
        smin = f"({t}.const {-(1 << (bits - 1))})"
        trap_s = (f"({t}.or ({t}.eqz (local.get $b)) ({t}.and ({t}.eq (local.get $a) {smin}) "
                  f"({t}.eq (local.get $b) ({t}.const -1))))")
        # i64.eqz and i64.eq give i32; i32.or/i32.and combine them for both widths.
        trap_s = trap_s.replace(f"({t}.or ", "(i32.or ").replace(f"({t}.and ", "(i32.and ")
        q_s = f"({t}.shr_s (local.get $a) ({t}.const {bits - 1}))"  # -1 for a negative dividend, else 0
        out.append(f""" (func {OPS[t + '.div_s']} (param $a {t}) (param $b {t}) (result {t})
  (if (result {t}) {trap_s}
   (then {q_s})
   (else ({t}.div_s (local.get $a) (local.get $b)))))""")
        out.append(f""" (func {OPS[t + '.rem_s']} (param $a {t}) (param $b {t}) (result {t})
  (if (result {t}) {trap_s}
   (then ({t}.sub (local.get $a) ({t}.mul {q_s} (local.get $b))))
   (else ({t}.rem_s (local.get $a) (local.get $b)))))""")
        out.append(f""" (func {OPS[t + '.div_u']} (param $a {t}) (param $b {t}) (result {t})
  (if (result {t}) ({t}.eqz (local.get $b))
   (then ({t}.const 0))
   (else ({t}.div_u (local.get $a) (local.get $b)))))""")
        out.append(f""" (func {OPS[t + '.rem_u']} (param $a {t}) (param $b {t}) (result {t})
  (if (result {t}) ({t}.eqz (local.get $b))
   (then (local.get $a))
   (else ({t}.rem_u (local.get $a) (local.get $b)))))""")
    return "\n".join(out) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--features", default=DEFAULT_FEATURES)
    parser.add_argument("--bin", help="directory with wasm-dis and wasm-as (default: PATH, then $EMSDK)")
    args = parser.parse_args()

    def tool(name: str) -> str:
        candidates = [os.path.join(args.bin, name)] if args.bin else []
        found = shutil.which(name)
        if found:
            candidates.append(found)
        if os.environ.get("EMSDK"):
            candidates.append(os.path.join(os.environ["EMSDK"], "upstream", "bin", name))
        for c in candidates:
            if os.path.isfile(c):
                return c
        sys.exit(f"error: {name} not found (Binaryen; use --bin)")

    features = [f"--enable-{f.strip()}" for f in args.features.split(",") if f.strip()]
    op_pattern = re.compile(r"\((i32|i64)\.(div_s|div_u|rem_s|rem_u)(?=[\s)])")
    counts = dict.fromkeys(OPS, 0)

    with tempfile.TemporaryDirectory() as tmp:
        text_in = os.path.join(tmp, "in.wat")
        text_out = os.path.join(tmp, "out.wat")
        subprocess.run([tool("wasm-dis"), *features, args.input, "-o", text_in], check=True)

        def rename(match: re.Match) -> str:
            op = f"{match.group(1)}.{match.group(2)}"
            counts[op] += 1
            return f"(call {OPS[op]}"

        with open(text_in, encoding="utf-8") as src, open(text_out, "w", encoding="utf-8") as dst:
            last = None
            for line in src:
                if not line.strip():
                    continue
                # Data segments are printed as strings; only code is rewritten.
                if not line.startswith(" (data") and "." in line:
                    line = op_pattern.sub(rename, line)
                if last is not None:
                    dst.write(last)
                last = line
            if last is None or last.rstrip() != ")":
                sys.exit("error: unexpected end of the module's text")
            dst.write(helpers())
            dst.write(last)
        subprocess.run([tool("wasm-as"), *features, text_out, "-o", args.output], check=True)

    total = sum(counts.values())
    detail = ", ".join(f"{op} {n}" for op, n in counts.items() if n)
    print(f"{args.output}: {total} divisions rewritten ({detail or 'none'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
