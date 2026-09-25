#include <eshkol/eshkol.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <setjmp.h>

extern "C" void eshkol_get_raised_value(eshkol_tagged_value_t*);

#if defined(__GNUC__) || defined(__clang__)
extern "C" eshkol_tagged_value_t f32_conjugate_probe(
    eshkol_tagged_value_t) __attribute__((weak));
#else
extern "C" eshkol_tagged_value_t f32_conjugate_probe(eshkol_tagged_value_t);
#endif

namespace {

constexpr std::array<uint32_t, 6> kPatterns = {
    UINT32_C(0x00000000), UINT32_C(0x80000000),
    UINT32_C(0x00000001), UINT32_C(0xbfc00000),
    UINT32_C(0x7f800000), UINT32_C(0x7fc12345),
};

int g_checks;
bool g_failed;

void check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: f32 conjugate: %s\n", message);
    g_failed = true;
}

double promoted(uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return std::isnan(value)
        ? [] {
              const uint64_t qnan = UINT64_C(0x7ff8000000000000);
              double fixed = 0.0;
              std::memcpy(&fixed, &qnan, sizeof(fixed));
              return fixed;
          }()
        : static_cast<double>(value);
}

uint64_t double_bits(double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

using Unary = eshkol_tagged_value_t (*)(eshkol_tagged_value_t);

void require_rejection(Unary fn, eshkol_tagged_value_t value,
                       const char* label) {
    jmp_buf handler;
    eshkol_push_exception_handler(&handler);
    if (setjmp(handler) == 0) {
        (void)fn(value);
        check(false, label);
    } else {
        eshkol_tagged_value_t raised{};
        eshkol_get_raised_value(&raised);
        check(raised.type == ESHKOL_VALUE_HEAP_PTR && raised.data.ptr_val != 0,
              "rejection did not produce an exception value");
        if (raised.type == ESHKOL_VALUE_HEAP_PTR && raised.data.ptr_val != 0) {
            const auto* exception = reinterpret_cast<const eshkol_exception_t*>(
                static_cast<uintptr_t>(raised.data.ptr_val));
            check(exception->message &&
                      std::strcmp(exception->message,
                                  "invalid or folded float32 value") == 0,
                  "rejection diagnostic mismatch");
        }
    }
    eshkol_pop_exception_handler();
}

}  // namespace

extern "C" void f32_conjugate_reset(void) {
    g_checks = 0;
    g_failed = false;
}

extern "C" float f32_conjugate_value(int64_t index) {
    if (index < 0 || static_cast<size_t>(index) >= kPatterns.size()) {
        g_failed = true;
        return 0.0f;
    }
    float value = 0.0f;
    const uint32_t bits = kPatterns[static_cast<size_t>(index)];
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

extern "C" int64_t f32_conjugate_check(int64_t index, double actual) {
    if (index < 0 || static_cast<size_t>(index) >= kPatterns.size()) {
        g_failed = true;
        return 0;
    }
    const double expected = promoted(kPatterns[static_cast<size_t>(index)]);
    const bool equal = double_bits(actual) == double_bits(expected);
    ++g_checks;
    check(equal, "promoted DOUBLE value mismatch");
    return equal ? 1 : 0;
}

extern "C" int f32_conjugate_test_direct(uint64_t address) {
    const bool prior_failure = g_failed;
    g_failed = false;
    Unary fn = reinterpret_cast<Unary>(static_cast<uintptr_t>(address));
    check(fn != nullptr, "missing direct conjugate probe");
    if (!fn) {
        g_failed = true;
        return 0;
    }

    for (uint32_t bits : kPatterns) {
        eshkol_tagged_value_t value{};
        check(eshkol_value_f32_from_bits_v1(&value, bits) == ESHKOL_VALUE_F32_OK,
              "canonical fixture construction failed");
        const eshkol_tagged_value_t result = fn(value);
        check(result.type == ESHKOL_VALUE_DOUBLE &&
                  result.flags == ESHKOL_VALUE_INEXACT_FLAG &&
                  result.reserved == 0,
              "canonical f32 conjugate did not return DOUBLE");
        const double expected = promoted(bits);
        const bool equal =
            double_bits(result.data.double_val) == double_bits(expected);
        check(equal, "direct canonical conjugate changed promoted value");
    }

    eshkol_tagged_value_t base{};
    (void)eshkol_value_f32_from_bits_v1(&base, UINT32_C(0x3fc00000));
    std::array<eshkol_tagged_value_t, 6> invalid{};
    invalid.fill(base);
    invalid[0].flags = 0;
    invalid[1].reserved = 1;
    reinterpret_cast<unsigned char*>(&invalid[2])[4] = 1;
    invalid[3].data.raw_val |= UINT64_C(1) << 32;
    invalid[4].type = ESHKOL_VALUE_FLOAT32 | ESHKOL_VALUE_EXACT_FLAG;
    invalid[5].type = ESHKOL_VALUE_FLOAT32 | ESHKOL_VALUE_INEXACT_FLAG;
    for (size_t i = 0; i < invalid.size(); ++i) {
        require_rejection(fn, invalid[i],
                          "malformed/folded f32 conjugate returned normally");
    }
    const bool direct_failed = g_failed;
    g_failed = prior_failure || direct_failed;
    return direct_failed ? 0 : 1;
}

extern "C" int64_t f32_conjugate_finish(int64_t fixture_ok) {
    check(fixture_ok == 1, "language fixture failed");
    check(g_checks == static_cast<int>(kPatterns.size()),
          "language fixture skipped a result check");
    if (f32_conjugate_probe) {
        check(f32_conjugate_test_direct(
                  reinterpret_cast<uint64_t>(&f32_conjugate_probe)) == 1,
              "AOT direct probe failed");
    }
    if (!g_failed) std::puts("PASS: f32 conjugate parity");
    return g_failed ? 0 : 1;
}
