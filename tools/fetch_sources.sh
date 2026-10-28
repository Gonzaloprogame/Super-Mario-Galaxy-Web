#!/bin/sh
# Descarga las bases de código de referencia en third_party/ (ignorado por git), fijadas a commits concretos.
# Las copias no se modifican: las adaptaciones web viven en web/ y se aplican en tiempo de compilación.
#   tools/fetch_sources.sh [destino]   (por defecto: third_party/)
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEST=${1:-$ROOT/third_party}
mkdir -p "$DEST"

# nombre  url  commit. Las dependencias de Aurora (deps/) son las versiones que fija su extern/CMakeLists.txt
# (abseil 20240722.0, xxHash 0.8.3, fmt 12.1.0, Tracy 0.14.1, imgui 1.91.9b-docking); magic_enum 0.9.7 no lo
# descarga Aurora (llega con su Dawn precompilado). Se clonan con git: el proxy de esta sesión no permite descargar
# los archivos comprimidos de GitHub.
SOURCES="
nebula https://github.com/keepvaibin/nebula.git a32d9456de82b76cbc25c01108a1afef72bde509
aurora https://github.com/encounter/aurora.git d0ace68f26682caf9e75be552d32e0c7410ed79e
petari https://github.com/SMGCommunity/Petari.git 470e9cea44760a6c70851207a13c5caa68c4db37
deps/abseil https://github.com/abseil/abseil-cpp.git 4447c7562e3bc702ade25105912dce503f0c4010
deps/xxhash https://github.com/Cyan4973/xxHash.git e626a72bc2321cd320e953a0ccf1584cad60f363
deps/fmt https://github.com/fmtlib/fmt.git 407c905e45ad75fc29bf0f9bb7c5c2fd3475976f
deps/tracy https://github.com/wolfpld/tracy.git 30997d5ca6bb632cc10807a1da8a6d3de0aeeb3c
deps/imgui https://github.com/ocornut/imgui.git 4806a1924ff6181180bf5e4b8b79ab4394118875
deps/magic_enum https://github.com/Neargye/magic_enum.git e046b69a3736d314fad813e159b1c192eaef92cd
deps/zlib https://github.com/madler/zlib.git da607da739fa6047df13e66a2af6b8bec7c2a498
deps/libpng https://github.com/pnggroup/libpng.git 3061454d980de7d53608f594194cfac722721d2a
deps/sdl3 https://github.com/libsdl-org/SDL.git 8e37db5e797b6167f3a00d697d816a684bd259c7
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

# SQLite's amalgamation (Aurora's pipeline cache), from the libsqlite3-sys crate pinned in tools/sqlite/Cargo.lock.
SQLITE_DEST="$DEST/deps/sqlite"
if [ ! -f "$SQLITE_DEST/sqlite3.c" ]; then
    cargo fetch --locked --manifest-path "$ROOT/tools/sqlite/Cargo.toml" >/dev/null
    crate=$(cargo metadata --locked --format-version 1 --manifest-path "$ROOT/tools/sqlite/Cargo.toml" |
        python3 -I -c 'import json,sys; print(next(p["manifest_path"] for p in json.load(sys.stdin)["packages"] if p["name"]=="libsqlite3-sys"))')
    mkdir -p "$SQLITE_DEST"
    cp "$(dirname "$crate")/sqlite3/sqlite3.c" "$(dirname "$crate")/sqlite3/sqlite3.h" "$SQLITE_DEST/"
fi
echo "deps/sqlite $(sed -n 's/^#define SQLITE_VERSION *"\(.*\)"/\1/p' "$SQLITE_DEST/sqlite3.h")"
