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

extern "C" int64_t f32_reachability_finish(int64_t semantic_ok) {
    constexpr int64_t kExpectedSemanticMask = 8191;
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
