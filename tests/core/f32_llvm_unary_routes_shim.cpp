#include <eshkol/eshkol.h>
#include <eshkol/core/rational.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <setjmp.h>
#include <utility>

extern "C" void eshkol_get_raised_value(eshkol_tagged_value_t*);

#ifndef F32_UNARY_JIT
using Unary = void (*)(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_unary_plus_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_unary_multiply_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_unary_divide_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_unary_min_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_unary_max_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_unary_numerator_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_unary_numerator_stored_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_unary_denominator_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_integer_gcd_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_integer_lcm_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_integer_gcd_stored_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_integer_lcm_stored_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_integer_gcd_stored_tail_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_integer_lcm_stored_tail_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_modulo_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_remainder_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_quotient_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_modulo_stored_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_remainder_stored_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void f32_quotient_stored_probe_bridge(eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
#endif

namespace {

constexpr std::array<uint32_t, 7> kPatterns = {
    UINT32_C(0x00000000), UINT32_C(0x80000000),
    UINT32_C(0x00000001), UINT32_C(0xbfc00000),
    UINT32_C(0x7f800000), UINT32_C(0xff800000),
    UINT32_C(0x7fc12345),
};

#ifndef F32_UNARY_JIT
struct Route {
    Unary fn;
    int operation;
    const char* name;
};
#endif

int g_checks;
bool g_failed;
const char* g_current_probe = "fixture";

void check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: f32 unary routes [%s]: %s\n",
                 g_current_probe, message);
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

#ifndef F32_UNARY_JIT
eshkol_tagged_value_t invoke(Unary fn, const eshkol_tagged_value_t& value) {
    eshkol_tagged_value_t result{};
    fn(&result, &value);
    return result;
}

void require_rejection(Unary fn, eshkol_tagged_value_t value,
                       const char* expected_message =
                           "invalid or folded float32 value") {
    jmp_buf handler;
    eshkol_push_exception_handler(&handler);
    if (setjmp(handler) == 0) {
        (void)invoke(fn, value);
        check(false, "expected rejection returned normally");
    } else {
        eshkol_tagged_value_t raised{};
        eshkol_get_raised_value(&raised);
        check(raised.type == ESHKOL_VALUE_HEAP_PTR && raised.data.ptr_val != 0,
              "rejection did not produce an exception value");
        if (raised.type == ESHKOL_VALUE_HEAP_PTR && raised.data.ptr_val != 0) {
            const auto* exception = reinterpret_cast<const eshkol_exception_t*>(
                static_cast<uintptr_t>(raised.data.ptr_val));
            check(exception->message &&
                      std::strcmp(exception->message, expected_message) == 0,
                  "rejection diagnostic mismatch");
        }
    }
    eshkol_pop_exception_handler();
}

void require_integer_domain_rejection(Unary fn, uint32_t bits) {
    eshkol_tagged_value_t value{};
    check(eshkol_value_f32_from_bits_v1(&value, bits) == ESHKOL_VALUE_F32_OK,
          "integer-domain fixture construction failed");
    jmp_buf handler;
    eshkol_push_exception_handler(&handler);
    if (setjmp(handler) == 0) {
        (void)invoke(fn, value);
        check(false, "invalid integer-domain F32 returned normally");
    } else {
        eshkol_tagged_value_t raised{};
        eshkol_get_raised_value(&raised);
        check(raised.type == ESHKOL_VALUE_HEAP_PTR && raised.data.ptr_val != 0,
              "integer-domain rejection did not raise");
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

    const eshkol_tagged_value_t int_result = invoke(route.fn, integer);
    const eshkol_tagged_value_t real_result = invoke(route.fn, real);
    if (route.operation == 2) {
        const bool rational = int_result.type == ESHKOL_VALUE_HEAP_PTR &&
            int_result.data.ptr_val != 0 &&
            ESHKOL_GET_HEADER(reinterpret_cast<void*>(
                static_cast<uintptr_t>(int_result.data.ptr_val)))->subtype ==
                HEAP_SUBTYPE_RATIONAL;
        check(rational, "integer unary division lost exact rational result");
        if (rational) {
            const auto* half = reinterpret_cast<const eshkol_rational_t*>(
                static_cast<uintptr_t>(int_result.data.ptr_val));
            check(!half->is_big && half->numerator == 1 &&
                      half->denominator == 2,
                  "integer unary division changed exact reciprocal");
        }
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
    g_current_probe = route.name;
    for (uint32_t bits : kPatterns) {
        eshkol_tagged_value_t value{};
        check(eshkol_value_f32_from_bits_v1(&value, bits) ==
                  ESHKOL_VALUE_F32_OK,
              "canonical fixture construction failed");
        const eshkol_tagged_value_t result = invoke(route.fn, value);
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
    g_current_probe = "denominator";
    for (uint32_t bits : kPatterns) {
        eshkol_tagged_value_t value{};
        check(eshkol_value_f32_from_bits_v1(&value, bits) ==
                  ESHKOL_VALUE_F32_OK,
              "denominator F32 fixture construction failed");
        const eshkol_tagged_value_t result =
            invoke(f32_unary_denominator_probe_bridge, value);
        check(result.type == ESHKOL_VALUE_INT64 && result.data.int_val == 1,
              "denominator F32 did not return INT64 1");
    }
    eshkol_tagged_value_t integer = eshkol_make_int64(3, true);
    eshkol_tagged_value_t real = eshkol_make_double(-2.5);
    for (const eshkol_tagged_value_t value : {integer, real}) {
        const eshkol_tagged_value_t result =
            invoke(f32_unary_denominator_probe_bridge, value);
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
        require_rejection(f32_unary_denominator_probe_bridge, value);
}

void check_aot_integer_helpers() {
    const Unary helpers[] = {
        f32_integer_gcd_probe_bridge, f32_integer_lcm_probe_bridge,
        f32_integer_gcd_stored_probe_bridge, f32_integer_lcm_stored_probe_bridge,
    };
    eshkol_tagged_value_t canonical{};
    (void)eshkol_value_f32_from_bits_v1(&canonical, UINT32_C(0xc0c00000));
    for (Unary helper : helpers) {
        g_current_probe = "integer helper";
        const bool is_gcd = helper == f32_integer_gcd_probe_bridge ||
                            helper == f32_integer_gcd_stored_probe_bridge;
        for (const auto& fixture : {
                 std::pair<uint32_t, double>{UINT32_C(0x00000000), is_gcd ? 4.0 : 0.0},
                 {UINT32_C(0x80000000), is_gcd ? 4.0 : 0.0},
                 {UINT32_C(0xc0c00000), is_gcd ? 2.0 : 12.0},
                 {UINT32_C(0x5e800000), is_gcd ? 4.0 : 0x1p62}}) {
            eshkol_tagged_value_t value{};
            (void)eshkol_value_f32_from_bits_v1(&value, fixture.first);
            const eshkol_tagged_value_t result = invoke(helper, value);
            check(result.type == ESHKOL_VALUE_DOUBLE &&
                      result.flags == ESHKOL_VALUE_INEXACT_FLAG &&
                      double_bits(result.data.double_val) == double_bits(fixture.second),
                  "canonical integer F32 did not yield the DOUBLE result");
        }
        for (uint32_t bits : {UINT32_C(0x3fc00000), UINT32_C(0x00000001),
                              UINT32_C(0x7f800000), UINT32_C(0xff800000),
                              UINT32_C(0x7fc12345), UINT32_C(0x5f000000),
                              UINT32_C(0xdf000000)})
            require_integer_domain_rejection(helper, bits);

        std::array<eshkol_tagged_value_t, 6> malformed{};
        malformed.fill(canonical);
        malformed[0].flags = 0;
        malformed[1].reserved = 1;
        reinterpret_cast<unsigned char*>(&malformed[2])[4] = 1;
        malformed[3].data.raw_val |= UINT64_C(1) << 32;
        malformed[4].type = ESHKOL_VALUE_FLOAT32 | ESHKOL_VALUE_EXACT_FLAG;
        malformed[5].type = ESHKOL_VALUE_FLOAT32 | ESHKOL_VALUE_INEXACT_FLAG;
        for (const eshkol_tagged_value_t value : malformed)
            require_rejection(helper, value);

        const eshkol_tagged_value_t control = invoke(helper, eshkol_make_int64(6, true));
        check(control.type == ESHKOL_VALUE_INT64 &&
                  control.data.int_val == (helper == f32_integer_gcd_probe_bridge ||
                                           helper == f32_integer_gcd_stored_probe_bridge ? 2 : 12),
              "integer helper non-F32 control changed");
        const eshkol_tagged_value_t integral = invoke(helper, eshkol_make_double(6.0));
        check(integral.type == ESHKOL_VALUE_DOUBLE &&
                  integral.data.double_val == (helper == f32_integer_gcd_probe_bridge ||
                                            helper == f32_integer_gcd_stored_probe_bridge ? 2 : 12),
              "integer helper integral DOUBLE control changed");
        const eshkol_tagged_value_t negative_zero = invoke(helper, eshkol_make_double(-0.0));
        const double expected_zero = helper == f32_integer_gcd_probe_bridge ||
                                     helper == f32_integer_gcd_stored_probe_bridge ? 4.0 : 0.0;
        check(negative_zero.type == ESHKOL_VALUE_DOUBLE &&
                  double_bits(negative_zero.data.double_val) == double_bits(expected_zero),
              "integer helper signed-zero DOUBLE result changed");
        for (double invalid : {6.5, 0x1p63, -0x1p63,
                               std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()})
            require_rejection(helper, eshkol_make_double(invalid),
                              is_gcd
                                  ? "gcd: expected a finite int64-valued number"
                                  : "lcm: expected a finite int64-valued number");
    }
    // A third stored operand must be consumed with the same F32 domain.
    for (Unary tail : {f32_integer_gcd_stored_tail_probe_bridge,
                       f32_integer_lcm_stored_tail_probe_bridge}) {
        g_current_probe = "integer tail";
        const eshkol_tagged_value_t accepted = invoke(tail, canonical);
        check(accepted.type == ESHKOL_VALUE_DOUBLE &&
                  accepted.data.double_val ==
                      (tail == f32_integer_gcd_stored_tail_probe_bridge ? 2.0 : 12.0),
              "stored variadic integer helper lost F32 result kind or value");
        require_integer_domain_rejection(tail, UINT32_C(0x3fc00000));
        eshkol_tagged_value_t malformed = canonical;
        malformed.reserved = 1;
        require_rejection(tail, malformed);
        const eshkol_tagged_value_t third = invoke(tail, eshkol_make_int64(3, true));
        const int64_t expected = tail == f32_integer_gcd_stored_tail_probe_bridge ? 1 : 12;
        check(third.type == ESHKOL_VALUE_INT64 && third.data.int_val == expected,
              "stored integer helper silently dropped its third operand");
        const eshkol_tagged_value_t inexact = invoke(tail, eshkol_make_double(6.0));
        check(inexact.type == ESHKOL_VALUE_DOUBLE &&
                  inexact.data.double_val == (tail == f32_integer_gcd_stored_tail_probe_bridge ? 2.0 : 12.0),
              "stored integer helper changed third-operand DOUBLE result kind");
    }
}

void check_aot_inexact_reduction_carriers() {
    const Unary helpers[] = {
        f32_modulo_probe_bridge, f32_remainder_probe_bridge, f32_quotient_probe_bridge,
        f32_modulo_stored_probe_bridge, f32_remainder_stored_probe_bridge,
        f32_quotient_stored_probe_bridge,
    };
    eshkol_tagged_value_t canonical{};
    (void)eshkol_value_f32_from_bits_v1(&canonical, UINT32_C(0xbfc00000));
    for (Unary helper : helpers) {
        g_current_probe = "inexact reduction";
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

#endif  // !F32_UNARY_JIT

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

extern "C" float f32_integer_value(void) {
    const uint32_t bits = UINT32_C(0xc0c00000); // -6.0, from host bits
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

extern "C" float f32_integer_large_value(void) {
    const uint32_t bits = UINT32_C(0x5e800000); // 2^62, from host bits
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
    check(g_checks == 71, "language fixture skipped a result check");
#ifndef F32_UNARY_JIT
    const Route routes[] = {
        {f32_unary_plus_probe_bridge, 0, "+"},
        {f32_unary_multiply_probe_bridge, 1, "*"},
        {f32_unary_divide_probe_bridge, 2, "/"},
        {f32_unary_min_probe_bridge, 3, "min"},
        {f32_unary_max_probe_bridge, 4, "max"},
        {f32_unary_numerator_probe_bridge, 0, "numerator"},
        {f32_unary_numerator_stored_probe_bridge, 0, "stored numerator"},
    };
    for (const Route& route : routes) check_aot_route(route);
    check_aot_denominator();
    check_aot_integer_helpers();
    check_aot_inexact_reduction_carriers();
#endif
    if (!g_failed) std::puts("PASS: f32 unary route promotion");
    return g_failed ? 0 : 1;
}
