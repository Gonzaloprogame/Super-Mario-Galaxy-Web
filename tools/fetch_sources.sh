#!/bin/sh
# Descarga las bases de código de referencia en third_party/ (ignorado por git), fijadas a commits concretos.
# Las copias no se modifican: las adaptaciones web viven en web/ y se aplican en tiempo de compilación.
#   tools/fetch_sources.sh [destino]   (por defecto: third_party/)
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST=${1:-$ROOT/third_party}
mkdir -p "$DEST"

# nombre  url  commit
SOURCES="
nebula https://github.com/keepvaibin/nebula.git a32d9456de82b76cbc25c01108a1afef72bde509
aurora https://github.com/encounter/aurora.git d0ace68f26682caf9e75be552d32e0c7410ed79e
petari https://github.com/SMGCommunity/Petari.git 470e9cea44760a6c70851207a13c5caa68c4db37
"

echo "$SOURCES" | while read -r name url commit; do
    [ -z "$name" ] && continue
    dir="$DEST/$name"
    if [ ! -d "$dir/.git" ]; then
        git init -q "$dir"
        git -C "$dir" remote add origin "$url"
    fi
    if [ "$(git -C "$dir" rev-parse -q --verify HEAD 2>/dev/null || true)" != "$commit" ]; then
        git -C "$dir" fetch -q --depth 1 origin "$commit"
        git -C "$dir" checkout -q --detach FETCH_HEAD
    fi
    if [ -n "$(git -C "$dir" status --porcelain)" ]; then
        echo "error: $dir tiene cambios locales; las copias de referencia deben quedar intactas" >&2
        exit 1
    fi
    echo "$name $(git -C "$dir" rev-parse --short HEAD)"
done
