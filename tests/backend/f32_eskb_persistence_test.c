#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../lib/backend/eskb_writer.c"
#include "../../lib/backend/eskb_reader.c"

static int failures;

#define CHECK(condition, message) do {                                      \
    if (!(condition)) {                                                     \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", message, __FILE__, __LINE__); \
        failures++;                                                         \
    }                                                                       \
} while (0)

static const uint8_t accepted_eskb_bytes[] = {
    0x42,0x4b,0x53,0x45,0x02,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
    0xac,0x3b,0xc4,0x4c,0x02,0x00,0x1a,0x01,0x0c,0x05,0x00,0x01,
    0xf9,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x02,0x00,0x00,0x00,
    0x00,0x00,0x00,0xf8,0x3f,0x03,0x01,0x06,0x02,0x43,0x32,0x01,
    0x04,0x6d,0x61,0x69,0x6e,0x00,0x00,0x00,0x01,0x24,0x00,
};

static int read_file(const char* path, uint8_t* out, size_t cap, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(out, 1, cap, f);
    int extra = fgetc(f);
    int close_result = fclose(f);
    if (size) *size = n;
    return close_result == 0 && extra == EOF ? 0 : -1;
}

static int write_bytes(const char* path, const void* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok ? 0 : -1;
}

int main(void) {
    char path[] = "/tmp/eshkol-f32-eskb-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0, "create temporary persistence path");
    if (fd < 0) return 1;
    close(fd);

    EskbConst constants[5];
    memset(constants, 0, sizeof(constants));
    constants[0].type = ESKB_CONST_NIL;
    constants[1].type = ESKB_CONST_INT64;
    constants[1].as.i = -7;
    constants[2].type = ESKB_CONST_F64;
    constants[2].as.f = 1.5;
    constants[3].type = ESKB_CONST_BOOL;
    constants[3].as.b = 1;
    constants[4].type = ESKB_CONST_STRING;
    memcpy(constants[4].str, "C2", 2);
    constants[4].str_len = 2;
    EskbInstr halt = {36, 0};

    CHECK(eskb_write_file(path, &halt, 1, constants, 5, NULL) == 0,
          "write accepted ESKB v2 constants");
    uint8_t actual[sizeof(accepted_eskb_bytes) + 1];
    size_t actual_size = 0;
    CHECK(read_file(path, actual, sizeof(actual), &actual_size) == 0,
          "read accepted ESKB bytes");
    CHECK(actual_size == sizeof(accepted_eskb_bytes) &&
              memcmp(actual, accepted_eskb_bytes, sizeof(accepted_eskb_bytes)) == 0,
          "accepted ESKB bytes remain byte-identical");

    EskbModule module;
    CHECK(eskb_load_memory(accepted_eskb_bytes, sizeof(accepted_eskb_bytes),
                           &module) == 0,
          "reader accepts established ESKB bytes");
    CHECK(module.n_constants == 5 &&
              module.const_types[0] == ESKB_CONST_NIL &&
              module.const_types[1] == ESKB_CONST_INT64 &&
              module.const_ints[1] == -7 &&
              module.const_types[2] == ESKB_CONST_F64 &&
              module.const_floats[2] == 1.5 &&
              module.const_types[3] == ESKB_CONST_BOOL &&
              module.const_ints[3] == 1 &&
              module.const_types[4] == ESKB_CONST_STRING &&
              strcmp(module.const_strings[4], "C2") == 0,
          "reader preserves established constant types and values");
    eskb_module_free(&module);

    static const uint8_t sentinel[] = {0x43, 0x32, 0xfa, 0x11, 0xed};
    const uint8_t f32_tags[] = {11, 34};
    for (size_t i = 0; i < sizeof(f32_tags); i++) {
        EskbConst unsupported;
        memset(&unsupported, 0, sizeof(unsupported));
        unsupported.type = f32_tags[i];
        unsupported.as.i = INT64_C(0x7f812345);
        CHECK(write_bytes(path, sentinel, sizeof(sentinel)) == 0,
              "seed rejected writer destination");
        CHECK(eskb_write_file(path, &halt, 1, &unsupported, 1, NULL) == -1,
              "writer rejects f32-shaped constant tag");
        actual_size = 0;
        CHECK(read_file(path, actual, sizeof(actual), &actual_size) == 0 &&
                  actual_size == sizeof(sentinel) &&
                  memcmp(actual, sentinel, sizeof(sentinel)) == 0,
              "rejected writer preserves destination bytes");

        uint8_t malformed[sizeof(accepted_eskb_bytes)];
        memcpy(malformed, accepted_eskb_bytes, sizeof(malformed));
        malformed[22] = f32_tags[i]; /* first constant type in CONST section */
        EskbHeader header;
        memcpy(&header, malformed, sizeof(header));
        header.checksum = eskb_crc32(malformed + sizeof(EskbHeader),
                                     sizeof(malformed) - sizeof(EskbHeader));
        memcpy(malformed, &header, sizeof(header));
        memset(&module, 0xa5, sizeof(module));
        CHECK(eskb_load_memory(malformed, sizeof(malformed), &module) == -1,
              "reader rejects f32-shaped constant tag");
        CHECK(module.const_types == NULL && module.const_ints == NULL &&
                  module.const_floats == NULL && module.n_constants == 0,
              "reader publishes no coerced f32 constant");
    }

    unlink(path);
    if (failures) return 1;
    puts("PASS: f32 ESKB persistence rejection");
    return 0;
}
