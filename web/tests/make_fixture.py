#!/usr/bin/env python3
"""Particiones sintéticas para pruebas, escritas por completo aquí (ningún byte procede de un disco de Nintendo).

Imitan solo la estructura que lee el host (boot.bin con el id del juego y los desplazamientos de main.dol y de la
tabla de ficheros, con carpetas vacías si las hay), con un main.dol vacío: el motor debe arrancar y detenerse al no
encontrar en él sus tablas.
    python3 -I web/tests/make_fixture.py <directorio>
"""
import os
import struct
import sys


def partition(game_id: bytes, directories: tuple = ()) -> bytes:
    data = bytearray(0x4000)
    data[0:6] = game_id
    # File table: the root directory, holding the empty directories given (by name), and their names.
    entries = 1 + len(directories)
    strings = bytearray()
    table = bytearray(struct.pack(">BBHII", 1, 0, 0, 0, entries))
    for index, name in enumerate(directories, start=1):
        table += struct.pack(">BBHII", 1, len(strings) >> 16, len(strings) & 0xFFFF, 0, index + 1)
        strings += name.encode("ascii") + b"\0"
    table += strings
    table += bytes(-len(table) % 4)
    data[0x3000:0x3000 + len(table)] = table
    # boot.bin: main.dol at 0x2000, the file table at 0x3000 (Wii partitions store them divided by 4).
    struct.pack_into(">III", data, 0x420, 0x2000 >> 2, 0x3000 >> 2, len(table) >> 2)
    return bytes(data)


def main() -> None:
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    fixtures = (("fixture_emptydol.pak", b"RMGE01", ()), ("fixture_wrongid.pak", b"ABCD01", ()),
                # Language folders as on the North American disc, but French (the host's choice of language)
                ("fixture_languages.pak", b"RMGE01", ("UsEnglish", "UsSpanish")))
    for name, game_id, directories in fixtures:
        with open(os.path.join(out, name), "wb") as f:
            f.write(partition(game_id, directories))


if __name__ == "__main__":
    main()
