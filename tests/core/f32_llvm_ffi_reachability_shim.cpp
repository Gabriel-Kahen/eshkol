#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t kPatterns[] = {
    UINT32_C(0x00000000),  // +0
    UINT32_C(0x80000000),  // -0
    UINT32_C(0x00000001),  // minimum subnormal
    UINT32_C(0x3fc00000),  // 1.5
    UINT32_C(0x7f800000),  // +infinity
    UINT32_C(0x7fc12345),  // quiet NaN with payload
    UINT32_C(0x7f812345),  // signaling NaN with payload
    UINT32_C(0x007fffff),  // maximum subnormal
    UINT32_C(0x00800000),  // minimum normal
    UINT32_C(0x7f7fffff),  // maximum finite
    UINT32_C(0x3f800000),  // 1.0
    UINT32_C(0xff800000),  // -infinity
};

int g_value_calls;
int g_check_calls;

bool valid_code(int64_t code) {
    return code >= 0 &&
           static_cast<uint64_t>(code) < sizeof(kPatterns) / sizeof(kPatterns[0]);
}

bool persistence_path(char* out, size_t size, int64_t pid) {
    return std::snprintf(out, size,
                         "/tmp/eshkol-f32-persistence-%lld.kb",
                         static_cast<long long>(pid)) > 0;
}

bool write_exact(FILE* file, const void* data, size_t size) {
    return std::fwrite(data, 1, size, file) == size;
}

}  // namespace

extern "C" void f32_reachability_reset(void) {
    g_value_calls = 0;
    g_check_calls = 0;
}

extern "C" float f32_reachability_value(int64_t code) {
    if (!valid_code(code)) return 0.0f;
    float value = 0.0f;
    const uint32_t bits = kPatterns[code];
    std::memcpy(&value, &bits, sizeof(value));
    ++g_value_calls;
    return value;
}

extern "C" int64_t f32_reachability_check_bits(int64_t code, float value) {
    if (!valid_code(code)) return 0;
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    ++g_check_calls;
    return bits == kPatterns[code] ? 1 : 0;
}

extern "C" int64_t f32_reachability_persistence_prepare(int64_t mode,
                                                          int64_t pid) {
    char path[128];
    if (!persistence_path(path, sizeof(path), pid)) return 0;
    FILE* file = std::fopen(path, "wb");
    if (!file) return 0;

    bool ok = false;
    if (mode == 0) {
        static const uint8_t sentinel[] = {0x43, 0x32, 0xfa, 0x11, 0xed};
        ok = write_exact(file, sentinel, sizeof(sentinel));
    } else if (mode == 1) {
        const uint32_t magic = UINT32_C(0x45534b42);
        const uint32_t version = 2;
        const uint32_t one = 1;
        const uint32_t zero = 0;
        const uint32_t name_length = 6;
        const char predicate[] = "metric";
        const uint8_t type = 11;
        const uint8_t flags = 0x20;
        const uint64_t signaling_nan = UINT64_C(0x7f812345);
        ok = write_exact(file, &magic, sizeof(magic)) &&
             write_exact(file, &version, sizeof(version)) &&
             write_exact(file, &one, sizeof(one)) &&
             write_exact(file, &zero, sizeof(zero)) &&
             write_exact(file, &name_length, sizeof(name_length)) &&
             write_exact(file, predicate, name_length) &&
             write_exact(file, &one, sizeof(one)) &&
             write_exact(file, &type, sizeof(type)) &&
             write_exact(file, &flags, sizeof(flags)) &&
             write_exact(file, &signaling_nan, sizeof(signaling_nan));
    }
    if (std::fclose(file) != 0) ok = false;
    if (!ok) std::remove(path);
    return ok ? 1 : 0;
}

extern "C" int64_t f32_reachability_persistence_check(int64_t mode,
                                                        int64_t pid) {
    char path[128];
    if (!persistence_path(path, sizeof(path), pid)) return 0;
    FILE* file = std::fopen(path, "rb");
    if (!file) return 0;
    uint8_t bytes[64];
    const size_t size = std::fread(bytes, 1, sizeof(bytes), file);
    const bool eof = std::fgetc(file) == EOF;
    const bool closed = std::fclose(file) == 0;
    std::remove(path);
    if (!eof || !closed) return 0;

    if (mode == 0) {
        static const uint8_t sentinel[] = {0x43, 0x32, 0xfa, 0x11, 0xed};
        return size == sizeof(sentinel) &&
               std::memcmp(bytes, sentinel, sizeof(sentinel)) == 0;
    }
    if (mode == 1) {
        return size == 40 && bytes[30] == 11 && bytes[31] == 0x20;
    }
    return 0;
}

extern "C" int64_t f32_reachability_finish(int64_t semantic_ok) {
    constexpr int64_t kExpectedSemanticMask = 16383;
    const bool ok = semantic_ok == kExpectedSemanticMask &&
                    g_value_calls >= 14 && g_check_calls == 15;
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL: f32 LLVM FFI reachability "
                     "(semantic-mask=%lld expected=%lld values=%d checks=%d)\n",
                     static_cast<long long>(semantic_ok),
                     static_cast<long long>(kExpectedSemanticMask),
                     g_value_calls, g_check_calls);
        return 0;
    }
    std::puts("PASS: f32 LLVM FFI reachability");
    return 1;
}
