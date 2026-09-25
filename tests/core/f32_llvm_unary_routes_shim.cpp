#include <eshkol/eshkol.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <setjmp.h>

extern "C" void eshkol_get_raised_value(eshkol_tagged_value_t*);

#if defined(__GNUC__) || defined(__clang__)
#define F32_UNARY_WEAK __attribute__((weak))
#else
#define F32_UNARY_WEAK
#endif

extern "C" eshkol_tagged_value_t f32_unary_plus_probe(
    eshkol_tagged_value_t) F32_UNARY_WEAK;
extern "C" eshkol_tagged_value_t f32_unary_multiply_probe(
    eshkol_tagged_value_t) F32_UNARY_WEAK;
extern "C" eshkol_tagged_value_t f32_unary_divide_probe(
    eshkol_tagged_value_t) F32_UNARY_WEAK;
extern "C" eshkol_tagged_value_t f32_unary_min_probe(
    eshkol_tagged_value_t) F32_UNARY_WEAK;
extern "C" eshkol_tagged_value_t f32_unary_max_probe(
    eshkol_tagged_value_t) F32_UNARY_WEAK;
extern "C" eshkol_tagged_value_t f32_unary_denominator_probe(
    eshkol_tagged_value_t) F32_UNARY_WEAK;
extern "C" eshkol_tagged_value_t f32_integer_gcd_probe(
    eshkol_tagged_value_t) F32_UNARY_WEAK;
extern "C" eshkol_tagged_value_t f32_integer_lcm_probe(
    eshkol_tagged_value_t) F32_UNARY_WEAK;
extern "C" eshkol_tagged_value_t f32_integer_gcd_stored_probe(
    eshkol_tagged_value_t) F32_UNARY_WEAK;
extern "C" eshkol_tagged_value_t f32_integer_lcm_stored_probe(
    eshkol_tagged_value_t) F32_UNARY_WEAK;
extern "C" eshkol_tagged_value_t f32_modulo_probe(
    eshkol_tagged_value_t) F32_UNARY_WEAK;
extern "C" eshkol_tagged_value_t f32_remainder_probe(
    eshkol_tagged_value_t) F32_UNARY_WEAK;
extern "C" eshkol_tagged_value_t f32_quotient_probe(
    eshkol_tagged_value_t) F32_UNARY_WEAK;
extern "C" eshkol_tagged_value_t f32_modulo_stored_probe(
    eshkol_tagged_value_t) F32_UNARY_WEAK;
extern "C" eshkol_tagged_value_t f32_remainder_stored_probe(
    eshkol_tagged_value_t) F32_UNARY_WEAK;
extern "C" eshkol_tagged_value_t f32_quotient_stored_probe(
    eshkol_tagged_value_t) F32_UNARY_WEAK;

namespace {

constexpr std::array<uint32_t, 7> kPatterns = {
    UINT32_C(0x00000000), UINT32_C(0x80000000),
    UINT32_C(0x00000001), UINT32_C(0xbfc00000),
    UINT32_C(0x7f800000), UINT32_C(0xff800000),
    UINT32_C(0x7fc12345),
};

using Unary = eshkol_tagged_value_t (*)(eshkol_tagged_value_t);

struct Route {
    Unary fn;
    int operation;
    const char* name;
};

int g_checks;
bool g_failed;

void check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: f32 unary routes: %s\n", message);
    g_failed = true;
}

uint64_t double_bits(double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double promoted(uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    if (!std::isnan(value)) return static_cast<double>(value);
    const uint64_t canonical = UINT64_C(0x7ff8000000000000);
    double result = 0.0;
    std::memcpy(&result, &canonical, sizeof(result));
    return result;
}

double expected_value(int operation, uint32_t bits) {
    const double value = promoted(bits);
    return operation == 2 ? 1.0 / value : value;
}

void require_rejection(Unary fn, eshkol_tagged_value_t value) {
    jmp_buf handler;
    eshkol_push_exception_handler(&handler);
    if (setjmp(handler) == 0) {
        (void)fn(value);
        check(false, "malformed/folded carrier returned normally");
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

void check_non_f32_controls(const Route& route) {
    eshkol_tagged_value_t integer{};
    integer.type = ESHKOL_VALUE_INT64;
    integer.flags = ESHKOL_VALUE_EXACT_FLAG;
    integer.data.int_val = 2;
    eshkol_tagged_value_t real{};
    real.type = ESHKOL_VALUE_DOUBLE;
    real.flags = ESHKOL_VALUE_INEXACT_FLAG;
    real.data.double_val = -0.0;

    const eshkol_tagged_value_t int_result = route.fn(integer);
    const eshkol_tagged_value_t real_result = route.fn(real);
    if (route.operation == 2) {
        check(int_result.type == ESHKOL_VALUE_DOUBLE &&
                  int_result.data.double_val == 0.5,
              "integer unary division did not retain reciprocal semantics");
        check(real_result.type == ESHKOL_VALUE_DOUBLE &&
                  real_result.data.double_val == -INFINITY,
              "DOUBLE unary division did not retain reciprocal semantics");
    } else {
        check(int_result.type == ESHKOL_VALUE_INT64 &&
                  int_result.data.int_val == 2,
              "integer identity control changed result kind or value");
        check(real_result.type == ESHKOL_VALUE_DOUBLE &&
                  double_bits(real_result.data.double_val) ==
                      double_bits(real.data.double_val),
              "DOUBLE identity control changed value bits");
    }
}

void check_aot_route(const Route& route) {
    check(route.fn != nullptr, "missing exported AOT probe");
    if (!route.fn) return;
    for (uint32_t bits : kPatterns) {
        eshkol_tagged_value_t value{};
        check(eshkol_value_f32_from_bits_v1(&value, bits) ==
                  ESHKOL_VALUE_F32_OK,
              "canonical fixture construction failed");
        const eshkol_tagged_value_t result = route.fn(value);
        check(result.type == ESHKOL_VALUE_DOUBLE &&
                  result.flags == ESHKOL_VALUE_INEXACT_FLAG &&
                  result.reserved == 0,
              "canonical f32 did not return DOUBLE");
        check(double_bits(result.data.double_val) ==
                  double_bits(expected_value(route.operation, bits)),
              "canonical f32 result bits mismatch");
    }

    check_non_f32_controls(route);

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
    for (const eshkol_tagged_value_t value : invalid) {
        require_rejection(route.fn, value);
    }
}

void check_aot_denominator() {
    check(f32_unary_denominator_probe != nullptr,
          "missing exported denominator AOT probe");
    if (!f32_unary_denominator_probe) return;
    for (uint32_t bits : kPatterns) {
        eshkol_tagged_value_t value{};
        check(eshkol_value_f32_from_bits_v1(&value, bits) ==
                  ESHKOL_VALUE_F32_OK,
              "denominator F32 fixture construction failed");
        const eshkol_tagged_value_t result =
            f32_unary_denominator_probe(value);
        check(result.type == ESHKOL_VALUE_INT64 && result.data.int_val == 1,
              "denominator F32 did not return INT64 1");
    }
    eshkol_tagged_value_t integer = eshkol_make_int64(3, true);
    eshkol_tagged_value_t real = eshkol_make_double(-2.5);
    for (const eshkol_tagged_value_t value : {integer, real}) {
        const eshkol_tagged_value_t result =
            f32_unary_denominator_probe(value);
        check(result.type == ESHKOL_VALUE_INT64 && result.data.int_val == 1,
              "denominator non-F32 AOT control changed");
    }
    eshkol_tagged_value_t malformed{};
    (void)eshkol_value_f32_from_bits_v1(&malformed, UINT32_C(0x3fc00000));
    std::array<eshkol_tagged_value_t, 6> invalid{};
    invalid.fill(malformed);
    invalid[0].flags = 0;
    invalid[1].reserved = 1;
    reinterpret_cast<unsigned char*>(&invalid[2])[4] = 1;
    invalid[3].data.raw_val |= UINT64_C(1) << 32;
    invalid[4].type = ESHKOL_VALUE_FLOAT32 | ESHKOL_VALUE_EXACT_FLAG;
    invalid[5].type = ESHKOL_VALUE_FLOAT32 | ESHKOL_VALUE_INEXACT_FLAG;
    for (const eshkol_tagged_value_t value : invalid)
        require_rejection(f32_unary_denominator_probe, value);
}

void check_aot_integer_helpers() {
    const Unary helpers[] = {
        f32_integer_gcd_probe, f32_integer_lcm_probe,
        f32_integer_gcd_stored_probe, f32_integer_lcm_stored_probe,
    };
    eshkol_tagged_value_t canonical{};
    (void)eshkol_value_f32_from_bits_v1(&canonical, UINT32_C(0x3fc00000));
    for (Unary helper : helpers) {
        check(helper != nullptr, "missing exported integer-helper probe");
        if (!helper) continue;
        jmp_buf handler;
        eshkol_push_exception_handler(&handler);
        if (setjmp(handler) == 0) {
            (void)helper(canonical);
            check(false, "integer helper accepted canonical f32");
        }
        eshkol_pop_exception_handler();

        eshkol_tagged_value_t malformed = canonical;
        malformed.reserved = 1;
        require_rejection(helper, malformed);
        malformed = canonical;
        malformed.type = ESHKOL_VALUE_FLOAT32 | ESHKOL_VALUE_EXACT_FLAG;
        require_rejection(helper, malformed);

        const eshkol_tagged_value_t control = helper(eshkol_make_int64(6, true));
        check(control.type == ESHKOL_VALUE_INT64 &&
                  control.data.int_val == (helper == f32_integer_gcd_probe ||
                                           helper == f32_integer_gcd_stored_probe ? 2 : 12),
              "integer helper non-F32 control changed");
    }
}

void check_aot_inexact_reduction_carriers() {
    const Unary helpers[] = {
        f32_modulo_probe, f32_remainder_probe, f32_quotient_probe,
        f32_modulo_stored_probe, f32_remainder_stored_probe,
        f32_quotient_stored_probe,
    };
    eshkol_tagged_value_t canonical{};
    (void)eshkol_value_f32_from_bits_v1(&canonical, UINT32_C(0xbfc00000));
    for (Unary helper : helpers) {
        check(helper != nullptr, "missing exported inexact-reduction probe");
        if (!helper) continue;
        eshkol_tagged_value_t malformed = canonical;
        malformed.flags = 0;
        require_rejection(helper, malformed);
        malformed = canonical;
        malformed.data.raw_val |= UINT64_C(1) << 32;
        require_rejection(helper, malformed);
        malformed = canonical;
        malformed.type = ESHKOL_VALUE_FLOAT32 | ESHKOL_VALUE_INEXACT_FLAG;
        require_rejection(helper, malformed);
    }
}

}  // namespace

extern "C" void f32_unary_reset(void) {
    g_checks = 0;
    g_failed = false;
}

extern "C" float f32_unary_value(int64_t index) {
    if (index < 0 || static_cast<size_t>(index) >= kPatterns.size()) {
        g_failed = true;
        return 0.0f;
    }
    float value = 0.0f;
    const uint32_t bits = kPatterns[static_cast<size_t>(index)];
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

extern "C" float f32_modquot_positive_value(void) {
    const uint32_t bits = UINT32_C(0x40b00000); // +5.5, from host bits
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

extern "C" int64_t f32_unary_check(int64_t operation, int64_t index,
                                     double actual) {
    if (operation < 0 || operation > 4 || index < 0 ||
        static_cast<size_t>(index) >= kPatterns.size()) {
        g_failed = true;
        return 0;
    }
    const double expected = expected_value(
        static_cast<int>(operation), kPatterns[static_cast<size_t>(index)]);
    const bool equal = double_bits(actual) == double_bits(expected);
    ++g_checks;
    check(equal, "language result bits mismatch");
    return equal ? 1 : 0;
}

extern "C" int64_t f32_unary_finish(int64_t fixture_ok) {
    check(fixture_ok == 1, "language fixture failed");
    check(g_checks == 56, "language fixture skipped a result check");
    const Route routes[] = {
        {f32_unary_plus_probe, 0, "+"},
        {f32_unary_multiply_probe, 1, "*"},
        {f32_unary_divide_probe, 2, "/"},
        {f32_unary_min_probe, 3, "min"},
        {f32_unary_max_probe, 4, "max"},
    };
    if (f32_unary_plus_probe) {
        for (const Route& route : routes) check_aot_route(route);
        check_aot_denominator();
        check_aot_integer_helpers();
        check_aot_inexact_reduction_carriers();
    }
    if (!g_failed) std::puts("PASS: f32 unary route promotion");
    return g_failed ? 0 : 1;
}
