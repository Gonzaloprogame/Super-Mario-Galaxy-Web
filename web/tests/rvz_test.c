/* web/host/rvz_reader.c against the synthetic RVZ images of tools/rvz_fixture (which nod, an independent reader, has
   already read back): every byte of the game partition, in order and at random, the reader's errors, and the
   formats it refuses. Run by web/tests/run_rvz_tests.sh (Node, NODERAWFS).
     rvz_test <directory with the images> <variant>... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rvz_reader.h"

static int failures;

static void check(int ok, const char* what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static unsigned char* load(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* data = malloc(*size);
    if (fread(data, 1, *size, f) != *size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

static unsigned long long sState = 0x9E3779B97F4A7C15ull;
static unsigned long long next(void) {
    sState ^= sState << 13;
    sState ^= sState >> 7;
    sState ^= sState << 17;
    return sState;
}

static void test_variant(const char* dir, const char* name) {
    char path[512];
    char what[256];
    size_t pakSize = 0;
    snprintf(path, sizeof(path), "%s/%s.pak", dir, name);
    unsigned char* pak = load(path, &pakSize);
    snprintf(path, sizeof(path), "%s/%s.rvz", dir, name);
    FILE* file = fopen(path, "rb");
    if (pak == NULL || file == NULL) {
        snprintf(what, sizeof(what), "%s: the image and its expected partition can be read", name);
        check(0, what);
        return;
    }
    char error[256] = "";
    RvzReader* reader = rvz_open(file, error, sizeof(error));
    snprintf(what, sizeof(what), "%s: rvz_open (%s)", name, reader ? "ok" : error);
    check(reader != NULL, what);
    if (reader == NULL) {
        return;
    }
    snprintf(what, sizeof(what), "%s: game ID %s", name, rvz_disc_id(reader));
    check(strcmp(rvz_disc_id(reader), "RMGE01") == 0, what);
    snprintf(what, sizeof(what), "%s: partition size %llu (expected %zu)", name,
             (unsigned long long)rvz_partition_size(reader), pakSize);
    check(rvz_partition_size(reader) == pakSize, what);

    /* In order, with read sizes that cross group and segment boundaries at varying points */
    unsigned char* buffer = malloc(1 << 20);
    static const size_t kSizes[] = {1, 4093, 0x7C00, 65537, 0x1F000, 131, 0x20000 + 17};
    size_t offset = 0;
    size_t mismatch = (size_t)-1;
    for (int k = 0; offset < pakSize; k++) {
        size_t n = kSizes[k % (sizeof(kSizes) / sizeof(kSizes[0]))];
        if (n > pakSize - offset) {
            n = pakSize - offset;
        }
        if (!rvz_read(reader, offset, buffer, n) || memcmp(buffer, pak + offset, n) != 0) {
            mismatch = offset;
            break;
        }
        offset += n;
    }
    snprintf(what, sizeof(what), "%s: the whole partition, in order, byte for byte%s", name,
             mismatch == (size_t)-1 ? "" : " (differs)");
    check(mismatch == (size_t)-1, what);
    if (mismatch != (size_t)-1) {
        printf("  first difference in the read at 0x%zx: %s\n", mismatch, rvz_last_error(reader));
    }

    /* At random: 3000 reads of up to 300 KB */
    int bad = 0;
    for (int i = 0; i < 3000 && !bad; i++) {
        const size_t n = 1 + next() % 300000;
        const size_t at = next() % (pakSize - n);
        bad = !rvz_read(reader, at, buffer, n) || memcmp(buffer, pak + at, n) != 0;
    }
    snprintf(what, sizeof(what), "%s: 3000 random reads, byte for byte", name);
    check(!bad, what);

    snprintf(what, sizeof(what), "%s: a read past the partition's end fails", name);
    check(!rvz_read(reader, pakSize - 10, buffer, 11), what);
    free(buffer);
    free(pak);
    fclose(file);
}

/* The image `name` with its bytes at `at` replaced, written next to it */
static FILE* altered(const char* dir, const char* name, size_t at, const void* bytes, size_t n) {
    char path[512];
    size_t size = 0;
    snprintf(path, sizeof(path), "%s/%s.rvz", dir, name);
    unsigned char* data = load(path, &size);
    if (data == NULL) {
        return NULL;
    }
    memcpy(data + at, bytes, n);
    snprintf(path, sizeof(path), "%s/altered.rvz", dir);
    FILE* out = fopen(path, "wb");
    fwrite(data, 1, size, out);
    fclose(out);
    free(data);
    return fopen(path, "rb");
}

static void test_refused(const char* dir, const char* name) {
    char error[256];
    char what[384];
    FILE* f = altered(dir, name, 0, "WIA\x01", 4);
    error[0] = 0;
    check(f != NULL && rvz_open(f, error, sizeof(error)) == NULL && strstr(error, "WIA") != NULL,
          (snprintf(what, sizeof(what), "a WIA image is refused: \"%s\"", error), what));
    if (f) {
        fclose(f);
    }
    static const unsigned char bzip2[4] = {0, 0, 0, 2};
    f = altered(dir, name, 0x48 + 4, bzip2, 4);
    error[0] = 0;
    check(f != NULL && rvz_open(f, error, sizeof(error)) == NULL && strstr(error, "bzip2") != NULL,
          (snprintf(what, sizeof(what), "a bzip2-compressed image is refused: \"%s\"", error), what));
    if (f) {
        fclose(f);
    }
    f = altered(dir, name, 0, "RMGE", 4);
    error[0] = 0;
    check(f != NULL && rvz_open(f, error, sizeof(error)) == NULL,
          (snprintf(what, sizeof(what), "a file that is not RVZ is refused: \"%s\"", error), what));
    if (f) {
        fclose(f);
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: rvz_test <directory> <variant>...\n");
        return 2;
    }
    for (int i = 2; i < argc; i++) {
        test_variant(argv[1], argv[i]);
    }
    test_refused(argv[1], argv[2]);
    printf(failures ? "%d failures\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
