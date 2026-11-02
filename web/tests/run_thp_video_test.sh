#!/bin/sh
# Decodificación de vídeos THP del motor web (web/host/thp_video.cpp, web/tests/thp_video_test.cpp), tres veces:
#   1. nativo, con el encabezado original de Nebula (runtime/include/galaxy/fused_multiply_add.h);
#   2. nativo, con el encabezado web (web/host/compat/galaxy/fused_multiply_add.h);
#   3. WebAssembly en Node, con el encabezado web, como lo compila el motor.
# Las tres sumas de comprobación de los planos tienen que coincidir: el decodificador da en el navegador los mismos
# píxeles que en Nebula. Requiere c++ (C++20) y emcc en el PATH (source emsdk_env.sh).
#   web/tests/run_thp_video_test.sh [directorio de salida]
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$ROOT/build/thp-tests}
R=$ROOT/third_party/nebula/runtime
H=$ROOT/web/host
SRC="$ROOT/web/tests/thp_video_test.cpp $H/thp_video.cpp $R/src/native_thp_video.cpp"
mkdir -p "$OUT"

c++ -std=c++20 -O2 -I"$R/include" -I"$H" $SRC -o "$OUT/thp_nebula_header"
c++ -std=c++20 -O2 -I"$H/compat" -I"$R/include" -I"$H" $SRC -o "$OUT/thp_web_header"
em++ -std=c++20 -O2 -I"$H/compat" -I"$R/include" -I"$H" $SRC -sENVIRONMENT=node -o "$OUT/thp_web_header.js"

status=0
for run in "$OUT/thp_nebula_header" "$OUT/thp_web_header" "node $OUT/thp_web_header.js"; do
    echo "== $run"
    $run > "$OUT/last.txt" || status=1
    cat "$OUT/last.txt"
    grep -o 'checksum [0-9a-f]*' "$OUT/last.txt" >> "$OUT/checksums.txt" || status=1
done
count=$(sort -u "$OUT/checksums.txt" | wc -l)
rm -f "$OUT/checksums.txt"
if [ "$count" -ne 1 ]; then
    echo "FAIL: the three builds give different planes"
    status=1
else
    echo "The three builds give the same planes"
fi
exit $status
