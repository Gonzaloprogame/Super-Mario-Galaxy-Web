/* The game partition of a Wii disc image in Dolphin's RVZ format, read in place: what game.pak holds (the
   partition's decrypted data at its own offsets), without writing it out. RVZ stores Wii partition data decrypted
   and without its hashes, so no console key is needed. Format: Dolphin's docs/WiaAndRvz.md. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct RvzReader RvzReader;

/* Opens the game partition (type 0 in the disc's partition table) of the RVZ image `file` (left open, owned by the
   caller). NULL on failure, with the reason in `error`. Compression: none or Zstandard (Dolphin's default). */
RvzReader* rvz_open(FILE* file, char* error, size_t error_size);

/* The partition's data size: game.pak's size. */
uint64_t rvz_partition_size(const RvzReader* reader);

/* The disc's first 6 bytes (the game ID), from the image's header. */
const char* rvz_disc_id(const RvzReader* reader);

/* Reads `size` bytes at `offset` of the partition's data. 1 on success, 0 on a read or format error. Thread-safe. */
int rvz_read(RvzReader* reader, uint64_t offset, void* dst, size_t size);

/* The reason of the last failed rvz_read. */
const char* rvz_last_error(const RvzReader* reader);
