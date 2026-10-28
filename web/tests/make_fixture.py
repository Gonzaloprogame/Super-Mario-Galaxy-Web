#!/usr/bin/env python3
"""Particiones sintéticas para pruebas, escritas por completo aquí (ningún byte procede de un disco de Nintendo).

Imitan solo la estructura que lee el host (boot.bin con el id del juego y los desplazamientos de main.dol y de la
tabla de ficheros), con un main.dol vacío: el motor debe arrancar y detenerse al no encontrar en él sus tablas.
    python3 -I web/tests/make_fixture.py <directorio>
"""
import os
import struct
import sys


def partition(game_id: bytes) -> bytes:
    data = bytearray(0x4000)
    data[0:6] = game_id
    # boot.bin: main.dol at 0x2000, file table at 0x3000 of 0x10 bytes (Wii partitions store them divided by 4).
    struct.pack_into(">III", data, 0x420, 0x2000 >> 2, 0x3000 >> 2, 0x10 >> 2)
    # File table: the root directory, holding no file.
    struct.pack_into(">BBHII", data, 0x3000, 1, 0, 0, 0, 1)
    return bytes(data)


def main() -> None:
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, game_id in (("fixture_emptydol.pak", b"RMGE01"), ("fixture_wrongid.pak", b"ABCD01")):
        with open(os.path.join(out, name), "wb") as f:
            f.write(partition(game_id))


if __name__ == "__main__":
    main()
