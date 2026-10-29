/* The game partition of an RVZ disc image, read in place (rvz_reader.h). Follows Dolphin's description of the format
   (docs/WiaAndRvz.md in Dolphin's repository): the file header and the disc struct, the partition and raw data
   structs, and the groups (chunks) that hold the data, each compressed on its own, which is what makes random access
   possible. For Wii partition data a group holds, for each 0x8000-byte disc sector, its 0x7C00 bytes of decrypted
   data (the hashes are left out), after the group's hash exception lists. RVZ packing stores the discs' pseudorandom
   padding as the seed of its generator. */
#include "rvz_reader.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <zstd.h>

enum {
    kSectorSize = 0x8000,     /* a Wii disc sector */
    kSectorDataSize = 0x7C00, /* its data without the hashes */
    kExceptionSize = 22,      /* wia_exception_t: u16 offset, 20-byte hash */
    kDiscStructSize = 0xDC,   /* wia_disc_t */
    kPartStructSize = 0x30,   /* wia_part_t */
    kRawStructSize = 24,      /* wia_raw_data_t */
    kGroupStructSize = 12,    /* rvz_group_t */
    kCacheEntries = 8,
    kLfgWords = 521,
};

enum { kCompressionNone = 0, kCompressionZstd = 5 };

typedef struct {
    uint32_t firstSector;
    uint32_t sectors;
    uint32_t groupIndex;
    uint32_t groups;
} PartData;

typedef struct {
    uint64_t offset; /* rounded down to a sector, as the format requires */
    uint64_t size;
    uint32_t groupIndex;
    uint32_t groups;
} RawData;

typedef struct {
    uint64_t fileOffset;
    uint32_t size;       /* stored size */
    int compressed;      /* stored with the disc's compression (else as is) */
    uint32_t packedSize; /* 0: no RVZ packing */
} Group;

typedef struct {
    int valid;
    int partition;
    uint32_t group;
    uint64_t lastUse;
    uint8_t* data;
    size_t size;
} CacheEntry;

struct RvzReader {
    FILE* file;
    pthread_mutex_t lock;
    uint32_t compression;
    uint32_t chunkSize;
    uint8_t discHead[0x80];
    char id[7];
    RawData* raw;
    uint32_t rawCount;
    Group* groups;
    uint32_t groupCount;
    PartData part[2];
    uint64_t partSize;
    CacheEntry cache[kCacheEntries];
    uint64_t clock;
    uint8_t* stored; /* a group as stored */
    size_t storedCapacity;
    uint8_t* stream; /* a group decompressed: exception lists, then (packed) data */
    size_t streamCapacity;
    ZSTD_DCtx* zstd;
    char error[256];
};

static uint16_t be16(const uint8_t* p) {
    return (uint16_t)(p[0] << 8 | p[1]);
}

static uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint64_t be64(const uint8_t* p) {
    return (uint64_t)be32(p) << 32 | be32(p + 4);
}

static int fail(RvzReader* r, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(r->error, sizeof(r->error), format, args);
    va_end(args);
    return 0;
}

static int file_read(RvzReader* r, uint64_t offset, void* dst, size_t size) {
    if (fseeko(r->file, (off_t)offset, SEEK_SET) != 0 || fread(dst, 1, size, r->file) != size) {
        return fail(r, "the image cannot be read at 0x%llx (%zu bytes)", (unsigned long long)offset, size);
    }
    return 1;
}

static int reserve(uint8_t** buffer, size_t* capacity, size_t size) {
    if (size <= *capacity) {
        return 1;
    }
    uint8_t* grown = (uint8_t*)realloc(*buffer, size);
    if (grown == NULL) {
        return 0;
    }
    *buffer = grown;
    *capacity = size;
    return 1;
}

/* Decompresses `size` bytes of `src` (one Zstandard frame) into r->stream; its length in *length. */
static int decompress(RvzReader* r, const uint8_t* src, size_t size, size_t* length) {
    ZSTD_DCtx_reset(r->zstd, ZSTD_reset_session_only);
    ZSTD_inBuffer in = {src, size, 0};
    size_t written = 0;
    for (;;) {
        if (written == r->streamCapacity &&
            !reserve(&r->stream, &r->streamCapacity, r->streamCapacity ? r->streamCapacity * 2 : 1 << 20)) {
            return fail(r, "out of memory decompressing a group");
        }
        ZSTD_outBuffer out = {r->stream, r->streamCapacity, written};
        const size_t result = ZSTD_decompressStream(r->zstd, &out, &in);
        if (ZSTD_isError(result)) {
            return fail(r, "Zstandard: %s", ZSTD_getErrorName(result));
        }
        written = out.pos;
        if (result == 0) {
            break; /* the frame is complete */
        }
        if (in.pos == in.size && out.pos < out.size) {
            return fail(r, "a group's Zstandard data is truncated");
        }
    }
    *length = written;
    return 1;
}

/* ---- RVZ packing's pseudorandom padding: a lagged Fibonacci generator (f = xor, j = 32, k = 521) ---- */

typedef struct {
    uint32_t words[kLfgWords];
    size_t position; /* bytes output from words[] since the last advance */
} Lfg;

static void lfg_advance(Lfg* lfg) {
    for (size_t i = 0; i < 32; i++) {
        lfg->words[i] ^= lfg->words[i + kLfgWords - 32];
    }
    for (size_t i = 32; i < kLfgWords; i++) {
        lfg->words[i] ^= lfg->words[i - 32];
    }
}

static void lfg_seed(Lfg* lfg, const uint8_t* seed) {
    for (size_t i = 0; i < 17; i++) {
        lfg->words[i] = be32(seed + i * 4);
    }
    for (size_t i = 17; i < kLfgWords; i++) {
        lfg->words[i] = (lfg->words[i - 17] << 23) ^ (lfg->words[i - 16] >> 9) ^ lfg->words[i - 1];
    }
    for (int i = 0; i < 4; i++) {
        lfg_advance(lfg);
    }
    lfg->position = 0;
}

static uint8_t lfg_byte(Lfg* lfg) {
    const uint32_t word = lfg->words[lfg->position / 4];
    static const int kShifts[4] = {24, 18, 8, 0}; /* 18, not 16, as the discs' generator does */
    const uint8_t byte = (uint8_t)(word >> kShifts[lfg->position % 4]);
    if (++lfg->position == kLfgWords * 4) {
        lfg_advance(lfg);
        lfg->position = 0;
    }
    return byte;
}

/* Decodes RVZ packing: runs of data as is, or of padding from a 68-byte seed. `offset` is the output's position in
   the generator's frame (the disc for raw data, the partition's data for partition data): the padding continues a
   sequence restarted every 0x8000 bytes. */
static int unpack(RvzReader* r, const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize, uint64_t offset) {
    size_t inPos = 0;
    size_t outPos = 0;
    while (outPos < outSize) {
        if (inSize - inPos < 4) {
            return fail(r, "RVZ packing ends early");
        }
        uint32_t size = be32(in + inPos);
        inPos += 4;
        const int padding = (size & 0x80000000u) != 0;
        size &= 0x7FFFFFFFu;
        if (size > outSize - outPos) {
            return fail(r, "RVZ packing overruns its group");
        }
        if (padding) {
            if (inSize - inPos < 68) {
                return fail(r, "an RVZ padding seed is truncated");
            }
            Lfg lfg;
            lfg_seed(&lfg, in + inPos);
            inPos += 68;
            for (uint64_t skip = (offset + outPos) % kSectorSize; skip > 0; skip--) {
                (void)lfg_byte(&lfg);
            }
            for (uint32_t i = 0; i < size; i++) {
                out[outPos + i] = lfg_byte(&lfg);
            }
        } else {
            if (inSize - inPos < size) {
                return fail(r, "RVZ packed data is truncated");
            }
            memcpy(out + outPos, in + inPos, size);
            inPos += size;
        }
        outPos += size;
    }
    return 1;
}

/* Decodes group `index` into `out` (`size` bytes of data). Partition groups start with `exceptionLists` hash exception
   lists, which are skipped (they only matter to rebuild the encrypted disc). `offset` positions RVZ padding. */
static int decode_group(RvzReader* r, uint32_t index, int partition, uint8_t* out, size_t size, uint64_t offset) {
    if (index >= r->groupCount) {
        return fail(r, "group %u is not in the image (%u groups)", index, r->groupCount);
    }
    const Group* group = &r->groups[index];
    if (group->size == 0) {
        memset(out, 0, size); /* every byte 0 */
        return 1;
    }
    if (!reserve(&r->stored, &r->storedCapacity, group->size)) {
        return fail(r, "out of memory reading a group");
    }
    if (!file_read(r, group->fileOffset, r->stored, group->size)) {
        return 0;
    }
    const uint8_t* stream = r->stored;
    size_t streamSize = group->size;
    if (group->compressed) {
        if (!decompress(r, r->stored, group->size, &streamSize)) {
            return 0;
        }
        stream = r->stream;
    }
    size_t pos = 0;
    if (partition) {
        const uint32_t lists = r->chunkSize >= 0x200000 ? r->chunkSize / 0x200000 : 1;
        for (uint32_t i = 0; i < lists; i++) {
            if (streamSize - pos < 2) {
                return fail(r, "group %u: its hash exception lists are truncated", index);
            }
            const size_t bytes = 2 + (size_t)be16(stream + pos) * kExceptionSize;
            if (streamSize - pos < bytes) {
                return fail(r, "group %u: its hash exception lists are truncated", index);
            }
            pos += bytes;
        }
        if (!group->compressed) {
            pos = (pos + 3) & ~(size_t)3; /* stored as is: the data starts on a 4-byte boundary */
        }
    }
    const uint8_t* payload = stream + pos;
    const size_t payloadSize = streamSize - pos;
    if (group->packedSize != 0) {
        return unpack(r, payload, payloadSize, out, size, offset);
    }
    if (payloadSize < size) {
        return fail(r, "group %u holds %zu bytes, %zu expected", index, payloadSize, size);
    }
    memcpy(out, payload, size);
    return 1;
}

/* A decoded group, from the cache or decoded into it. */
static const uint8_t* cached_group(RvzReader* r, uint32_t index, int partition, size_t size, uint64_t offset) {
    CacheEntry* slot = NULL;
    for (int i = 0; i < kCacheEntries; i++) {
        CacheEntry* entry = &r->cache[i];
        if (entry->valid && entry->partition == partition && entry->group == index) {
            entry->lastUse = ++r->clock;
            return entry->data;
        }
        if (slot == NULL || !entry->valid || (slot->valid && entry->lastUse < slot->lastUse)) {
            slot = entry;
        }
    }
    if (slot->size < size) {
        uint8_t* grown = (uint8_t*)realloc(slot->data, size);
        if (grown == NULL) {
            fail(r, "out of memory caching a group");
            return NULL;
        }
        slot->data = grown;
        slot->size = size;
    }
    slot->valid = 0;
    if (!decode_group(r, index, partition, slot->data, size, offset)) {
        return NULL;
    }
    slot->valid = 1;
    slot->partition = partition;
    slot->group = index;
    slot->lastUse = ++r->clock;
    return slot->data;
}

/* Disc bytes outside the partitions' encrypted data (the disc header, the partition table, partition headers). */
static int read_raw(RvzReader* r, uint64_t offset, uint8_t* dst, size_t size) {
    while (size > 0) {
        if (offset < sizeof(r->discHead)) {
            const size_t n = size < sizeof(r->discHead) - offset ? size : sizeof(r->discHead) - (size_t)offset;
            memcpy(dst, r->discHead + offset, n);
            offset += n;
            dst += n;
            size -= n;
            continue;
        }
        const RawData* raw = NULL;
        for (uint32_t i = 0; i < r->rawCount; i++) {
            if (offset >= r->raw[i].offset && offset - r->raw[i].offset < r->raw[i].size) {
                raw = &r->raw[i];
                break;
            }
        }
        if (raw == NULL) {
            return fail(r, "disc offset 0x%llx is not in the image's raw data", (unsigned long long)offset);
        }
        const uint64_t group = (offset - raw->offset) / r->chunkSize;
        if (group >= raw->groups) {
            return fail(r, "disc offset 0x%llx is past its raw data's groups", (unsigned long long)offset);
        }
        const uint64_t groupStart = raw->offset + group * r->chunkSize;
        const uint64_t remaining = raw->offset + raw->size - groupStart;
        const size_t groupSize = (size_t)(remaining < r->chunkSize ? remaining : r->chunkSize);
        const uint8_t* data = cached_group(r, raw->groupIndex + (uint32_t)group, 0, groupSize, groupStart);
        if (data == NULL) {
            return 0;
        }
        const size_t within = (size_t)(offset - groupStart);
        const size_t n = size < groupSize - within ? size : groupSize - within;
        memcpy(dst, data + within, n);
        offset += n;
        dst += n;
        size -= n;
    }
    return 1;
}

static int read_partition(RvzReader* r, uint64_t offset, uint8_t* dst, size_t size) {
    const size_t groupData = (size_t)(r->chunkSize / kSectorSize) * kSectorDataSize;
    while (size > 0) {
        const PartData* part = NULL;
        uint64_t partStart = 0;
        for (int i = 0; i < 2; i++) {
            const uint64_t start = (uint64_t)(r->part[i].firstSector - r->part[0].firstSector) * kSectorDataSize;
            if (r->part[i].sectors != 0 && offset >= start &&
                offset - start < (uint64_t)r->part[i].sectors * kSectorDataSize) {
                part = &r->part[i];
                partStart = start;
                break;
            }
        }
        if (part == NULL) {
            return fail(r, "partition offset 0x%llx is outside its data", (unsigned long long)offset);
        }
        const uint64_t group = (offset - partStart) / groupData;
        if (group >= part->groups) {
            return fail(r, "partition offset 0x%llx is past its groups", (unsigned long long)offset);
        }
        const uint64_t groupStart = partStart + group * groupData;
        const uint64_t remaining = partStart + (uint64_t)part->sectors * kSectorDataSize - groupStart;
        const size_t groupSize = (size_t)(remaining < groupData ? remaining : groupData);
        const uint8_t* data = cached_group(r, part->groupIndex + (uint32_t)group, 1, groupSize, groupStart);
        if (data == NULL) {
            return 0;
        }
        const size_t within = (size_t)(offset - groupStart);
        const size_t n = size < groupSize - within ? size : groupSize - within;
        memcpy(dst, data + within, n);
        offset += n;
        dst += n;
        size -= n;
    }
    return 1;
}

/* A table of `count` structs of `entrySize` bytes stored at `offset`, compressed with the disc's method. */
static uint8_t* read_table(RvzReader* r, uint64_t offset, uint32_t storedSize, uint32_t count, size_t entrySize) {
    const size_t size = (size_t)count * entrySize;
    uint8_t* stored = (uint8_t*)malloc(storedSize ? storedSize : 1);
    uint8_t* table = (uint8_t*)malloc(size ? size : 1);
    if (stored == NULL || table == NULL) {
        free(stored);
        free(table);
        fail(r, "out of memory reading the image's tables");
        return NULL;
    }
    size_t length = storedSize;
    int ok = file_read(r, offset, stored, storedSize);
    if (ok && r->compression == kCompressionZstd) {
        ok = decompress(r, stored, storedSize, &length);
        if (ok && length >= size) {
            memcpy(table, r->stream, size);
        }
    } else if (ok && length >= size) {
        memcpy(table, stored, size);
    }
    free(stored);
    if (ok && length < size) {
        ok = fail(r, "a table of the image is truncated");
    }
    if (!ok) {
        free(table);
        return NULL;
    }
    return table;
}

static void rvz_free(RvzReader* r) {
    if (r == NULL) {
        return;
    }
    for (int i = 0; i < kCacheEntries; i++) {
        free(r->cache[i].data);
    }
    free(r->raw);
    free(r->groups);
    free(r->stored);
    free(r->stream);
    if (r->zstd != NULL) {
        ZSTD_freeDCtx(r->zstd);
    }
    pthread_mutex_destroy(&r->lock);
    free(r);
}

static RvzReader* open_fail(RvzReader* r, char* error, size_t errorSize) {
    snprintf(error, errorSize, "%s", r->error);
    rvz_free(r);
    return NULL;
}

RvzReader* rvz_open(FILE* file, char* error, size_t errorSize) {
    RvzReader* r = (RvzReader*)calloc(1, sizeof(RvzReader));
    if (r == NULL) {
        snprintf(error, errorSize, "out of memory");
        return NULL;
    }
    r->file = file;
    pthread_mutex_init(&r->lock, NULL);
    r->zstd = ZSTD_createDCtx();

    /* wia_file_head_t, then wia_disc_t */
    uint8_t head[0x48];
    if (!file_read(r, 0, head, sizeof(head))) {
        return open_fail(r, error, errorSize);
    }
    if (memcmp(head, "WIA\x01", 4) == 0) {
        fail(r, "this is a WIA image; only RVZ is supported (Dolphin converts WIA to RVZ)");
        return open_fail(r, error, errorSize);
    }
    if (memcmp(head, "RVZ\x01", 4) != 0) {
        fail(r, "not an RVZ image");
        return open_fail(r, error, errorSize);
    }
    const uint32_t compatible = be32(head + 8);
    if (compatible > 0x01000000u) {
        fail(r, "RVZ version 0x%08x is newer than this reader (1.00)", compatible);
        return open_fail(r, error, errorSize);
    }
    uint8_t disc[kDiscStructSize] = {0};
    const uint32_t discSize = be32(head + 12);
    if (!file_read(r, 0x48, disc, discSize < sizeof(disc) ? discSize : sizeof(disc))) {
        return open_fail(r, error, errorSize);
    }
    if (be32(disc + 0x00) != 2) {
        fail(r, "the image is not of a Wii disc (disc type %u)", be32(disc));
        return open_fail(r, error, errorSize);
    }
    r->compression = be32(disc + 0x04);
    if (r->compression != kCompressionNone && r->compression != kCompressionZstd) {
        static const char* const names[] = {"none", "purge", "bzip2", "LZMA", "LZMA2", "Zstandard"};
        fail(r, "the image is compressed with %s; this reader supports Zstandard (Dolphin's default) and none: "
                "convert it again in Dolphin with Zstandard",
             r->compression < 6 ? names[r->compression] : "an unknown method");
        return open_fail(r, error, errorSize);
    }
    r->chunkSize = be32(disc + 0x0C);
    const uint32_t chunk = r->chunkSize;
    if (chunk < kSectorSize || (chunk < 0x200000 && (chunk & (chunk - 1)) != 0) ||
        (chunk >= 0x200000 && chunk % 0x200000 != 0)) {
        fail(r, "the image's chunk size (0x%x) is invalid", chunk);
        return open_fail(r, error, errorSize);
    }
    memcpy(r->discHead, disc + 0x10, sizeof(r->discHead));
    memcpy(r->id, r->discHead, 6);
    r->id[6] = 0;

    /* The groups, then the raw data and partition structs */
    r->groupCount = be32(disc + 0xC4);
    uint8_t* groups = read_table(r, be64(disc + 0xC8), be32(disc + 0xD0), r->groupCount, kGroupStructSize);
    if (groups == NULL) {
        return open_fail(r, error, errorSize);
    }
    r->groups = (Group*)calloc(r->groupCount ? r->groupCount : 1, sizeof(Group));
    for (uint32_t i = 0; r->groups != NULL && i < r->groupCount; i++) {
        const uint8_t* g = groups + (size_t)i * kGroupStructSize;
        const uint32_t dataSize = be32(g + 4);
        r->groups[i].fileOffset = (uint64_t)be32(g) * 4;
        r->groups[i].size = dataSize & 0x7FFFFFFFu;
        r->groups[i].compressed = (dataSize & 0x80000000u) != 0 && r->compression != kCompressionNone;
        r->groups[i].packedSize = be32(g + 8);
    }
    free(groups);

    r->rawCount = be32(disc + 0xB4);
    uint8_t* raws = read_table(r, be64(disc + 0xB8), be32(disc + 0xC0), r->rawCount, kRawStructSize);
    if (raws == NULL || r->groups == NULL) {
        free(raws);
        return open_fail(r, error, errorSize);
    }
    r->raw = (RawData*)calloc(r->rawCount ? r->rawCount : 1, sizeof(RawData));
    for (uint32_t i = 0; r->raw != NULL && i < r->rawCount; i++) {
        const uint8_t* e = raws + (size_t)i * kRawStructSize;
        const uint64_t offset = be64(e);
        const uint64_t aligned = offset & ~(uint64_t)(kSectorSize - 1); /* the first one starts at 0x80 */
        r->raw[i].offset = aligned;
        r->raw[i].size = be64(e + 8) + (offset - aligned);
        r->raw[i].groupIndex = be32(e + 16);
        r->raw[i].groups = be32(e + 20);
    }
    free(raws);

    /* The game partition: type 0 in the partition table at 0x40000 (four groups of count and offset / 4) */
    uint8_t table[32];
    if (r->raw == NULL || !read_raw(r, 0x40000, table, sizeof(table))) {
        return open_fail(r, error, errorSize);
    }
    uint64_t gameData = 0;
    for (int t = 0; t < 4 && gameData == 0; t++) {
        const uint32_t count = be32(table + t * 8);
        const uint64_t at = (uint64_t)be32(table + t * 8 + 4) << 2;
        for (uint32_t i = 0; i < count && i < 64; i++) {
            uint8_t entry[8];
            if (!read_raw(r, at + i * 8, entry, sizeof(entry))) {
                return open_fail(r, error, errorSize);
            }
            if (be32(entry + 4) == 0) {
                const uint64_t partition = (uint64_t)be32(entry) << 2;
                uint8_t header[8];
                if (!read_raw(r, partition + 0x2B8, header, sizeof(header))) {
                    return open_fail(r, error, errorSize);
                }
                gameData = partition + ((uint64_t)be32(header) << 2);
                break;
            }
        }
    }
    if (gameData == 0) {
        fail(r, "the disc has no game partition");
        return open_fail(r, error, errorSize);
    }
    const uint32_t partCount = be32(disc + 0x90);
    const uint32_t partStride = be32(disc + 0x94);
    const uint64_t partOffset = be64(disc + 0x98);
    int found = 0;
    for (uint32_t i = 0; i < partCount && !found; i++) {
        uint8_t part[kPartStructSize] = {0};
        if (!file_read(r, partOffset + (uint64_t)i * partStride, part,
                       partStride < sizeof(part) ? partStride : sizeof(part))) {
            return open_fail(r, error, errorSize);
        }
        for (int s = 0; s < 2; s++) {
            const uint8_t* pd = part + 16 + s * 16;
            r->part[s] = (PartData){be32(pd), be32(pd + 4), be32(pd + 8), be32(pd + 12)};
        }
        found = (uint64_t)r->part[0].firstSector * kSectorSize == gameData;
    }
    if (!found) {
        fail(r, "the game partition's data (disc offset 0x%llx) is not in the image", (unsigned long long)gameData);
        return open_fail(r, error, errorSize);
    }
    for (int s = 0; s < 2; s++) {
        if (r->part[s].sectors == 0) {
            continue;
        }
        const uint64_t end =
            (uint64_t)(r->part[s].firstSector - r->part[0].firstSector + r->part[s].sectors) * kSectorDataSize;
        if (end > r->partSize) {
            r->partSize = end;
        }
    }
    return r;
}

uint64_t rvz_partition_size(const RvzReader* reader) {
    return reader->partSize;
}

const char* rvz_disc_id(const RvzReader* reader) {
    return reader->id;
}

int rvz_read(RvzReader* reader, uint64_t offset, void* dst, size_t size) {
    pthread_mutex_lock(&reader->lock);
    const int ok = offset <= reader->partSize && size <= reader->partSize - offset
                       ? read_partition(reader, offset, (uint8_t*)dst, size)
                       : fail(reader, "read past the partition's end");
    pthread_mutex_unlock(&reader->lock);
    return ok;
}

const char* rvz_last_error(const RvzReader* reader) {
    return reader->error;
}
