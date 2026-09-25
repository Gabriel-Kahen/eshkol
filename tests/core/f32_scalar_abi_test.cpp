/*
 * Copyright (C) tsotchke
 *
 * SPDX-License-Identifier: MIT
 */

#include <eshkol/eshkol.h>
#include <eshkol/eshkol_ffi.h>
#include <eshkol/core/bignum.h>
#include <eshkol/core/inference.h>
#include <eshkol/core/introspection.h>
#include <eshkol/core/logic.h>
#include <eshkol/core/rational.h>
#include <eshkol/core/runtime.h>

#include "../../lib/core/arena_memory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

extern "C" int64_t eshkol_unwrap_list_index(
    const eshkol_tagged_value_t* value);
extern "C" void* eshkol_tensor_from_collection(
    arena_t* arena, const eshkol_tagged_value_t* input);
extern "C" void* eshkol_list_to_svec(
    arena_t* arena, const eshkol_tagged_value_t* input);
extern "C" int64_t eshkol_ad_extract_doubles(
    const eshkol_tagged_value_t* input, double* out, int64_t max_n);
extern "C" int32_t eshkol_ad_point_is_scalar(
    const eshkol_tagged_value_t* value);
extern "C" double eshkol_ad_point_to_double(
    const eshkol_tagged_value_t* value, const char* operation);
extern "C" void eshkol_dnc_loc_address_tagged(
    arena_t*, const eshkol_tagged_value_t*, const eshkol_tagged_value_t*,
    const eshkol_tagged_value_t*, eshkol_tagged_value_t*);
extern "C" void eshkol_sdnc_program_tagged(
    arena_t*, const eshkol_tagged_value_t*, eshkol_tagged_value_t*);
extern "C" void eshkol_sdnc_run_tagged(
    arena_t*, const eshkol_tagged_value_t*, const eshkol_tagged_value_t*,
    eshkol_tagged_value_t*);
extern "C" double eshkol_taylor_c0(const eshkol_tagged_value_t*);
extern "C" void eshkol_taylor_binary_tagged(
    arena_t*, const eshkol_tagged_value_t*, const eshkol_tagged_value_t*, int,
    eshkol_tagged_value_t*);
extern "C" void eshkol_taylor_unary_tagged(
    arena_t*, const eshkol_tagged_value_t*, int, eshkol_tagged_value_t*);
extern "C" void eshkol_taylor_seed_tagged(
    arena_t*, const eshkol_tagged_value_t*, int32_t, eshkol_tagged_value_t*);
extern "C" double eshkol_taylor_extract(
    const eshkol_tagged_value_t*, uint32_t);
extern "C" void eshkol_ad_tape_new_sret(eshkol_tagged_value_t*);
extern "C" void eshkol_ad_tape_release_sret(
    eshkol_tagged_value_t*, const eshkol_tagged_value_t*);
extern "C" void eshkol_ad_const_sret(
    eshkol_tagged_value_t*, const eshkol_tagged_value_t*,
    const eshkol_tagged_value_t*);
extern "C" void eshkol_ad_var_sret(
    eshkol_tagged_value_t*, const eshkol_tagged_value_t*,
    const eshkol_tagged_value_t*);
extern "C" void eshkol_clear_current_exception(void);

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

uint64_t double_bits(double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double expected_promotion(uint32_t bits) {
    if ((bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) &&
        (bits & UINT32_C(0x007fffff)) != 0) {
        const uint64_t canonical_nan = UINT64_C(0x7ff8000000000000);
        double result;
        std::memcpy(&result, &canonical_nan, sizeof(result));
        return result;
    }
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return static_cast<double>(value);
}

bool canonical_bytes(const eshkol_tagged_value_t& value, uint32_t bits) {
    unsigned char bytes[sizeof(value)];
    std::memcpy(bytes, &value, sizeof(bytes));
    return value.type == ESHKOL_VALUE_FLOAT32 &&
           value.flags == ESHKOL_VALUE_INEXACT_FLAG &&
           value.reserved == 0 &&
           bytes[4] == 0 && bytes[5] == 0 && bytes[6] == 0 && bytes[7] == 0 &&
           value.data.raw_val == static_cast<uint64_t>(bits);
}

void require_failed_native_inspection(const eshkol_tagged_value_t& value,
                                      const char* message) {
    uint32_t output = UINT32_C(0xa5a55a5a);
    check(eshkol_value_is_f32_v1(&value) == 0, message);
    check(eshkol_value_f32_to_bits_v1(&value, &output) ==
              ESHKOL_VALUE_F32_INVALID_VALUE,
          message);
    check(output == UINT32_C(0xa5a55a5a),
          "failed native inspection changed output");
}

void test_layout_and_round_trip() {
    static_assert(ESHKOL_VALUE_FLOAT32 == 11);
    static_assert(ESHKOL_FFI_TYPE_FLOAT32 == 11);
    static_assert(ESHKOL_HAS_F32_SCALAR_ABI_V1 == 1);
    static_assert(static_cast<int>(ESHKOL_FFI_F32_OK) ==
                  static_cast<int>(ESHKOL_VALUE_F32_OK));
    static_assert(static_cast<int>(ESHKOL_FFI_F32_INVALID_ARGUMENT) ==
                  static_cast<int>(ESHKOL_VALUE_F32_INVALID_ARGUMENT));
    static_assert(static_cast<int>(ESHKOL_FFI_F32_INVALID_VALUE) ==
                  static_cast<int>(ESHKOL_VALUE_F32_INVALID_VALUE));
    static_assert(!ESHKOL_IS_INT_STORAGE_TYPE(
        ESHKOL_VALUE_FLOAT32 | ESHKOL_VALUE_INEXACT_FLAG));
    static_assert(sizeof(eshkol_tagged_value_t) == 16);
    static_assert(alignof(eshkol_tagged_value_t) == 8);
    static_assert(offsetof(eshkol_tagged_value_t, type) == 0);
    static_assert(offsetof(eshkol_tagged_value_t, flags) == 1);
    static_assert(offsetof(eshkol_tagged_value_t, reserved) == 2);
    static_assert(offsetof(eshkol_tagged_value_t, data) == 8);
    static_assert(sizeof(eshkol_tagged_value_t) == sizeof(eshkol_ffi_value_t));
    static_assert(alignof(eshkol_tagged_value_t) == alignof(eshkol_ffi_value_t));
    static_assert(offsetof(eshkol_tagged_value_t, data) ==
                  offsetof(eshkol_ffi_value_t, data));

    check(eshkol_runtime_has_f32_scalar_v1() == 1,
          "runtime f32 feature probe did not return one");

    constexpr std::array<uint32_t, 13> patterns = {
        UINT32_C(0x00000000),  // +0
        UINT32_C(0x80000000),  // -0
        UINT32_C(0x00000001),  // minimum subnormal
        UINT32_C(0x007fffff),  // maximum subnormal
        UINT32_C(0x00800000),  // minimum normal
        UINT32_C(0x3f800000),  // 1
        UINT32_C(0x7f7fffff),  // maximum finite
        UINT32_C(0x7f800000),  // +infinity
        UINT32_C(0xff800000),  // -infinity
        UINT32_C(0x7fc12345),  // quiet NaN payload
        UINT32_C(0xffc54321),  // signed quiet NaN payload
        UINT32_C(0x7f800001),  // signaling NaN payload
        UINT32_C(0xff800001),  // signed signaling NaN payload
    };

    for (uint32_t bits : patterns) {
        eshkol_tagged_value_t native;
        std::memset(&native, 0xa5, sizeof(native));
        check(eshkol_value_f32_from_bits_v1(&native, bits) == ESHKOL_VALUE_F32_OK,
              "native f32 construction failed");
        check(canonical_bytes(native, bits),
              "native f32 construction was not byte-canonical");
        check(eshkol_value_is_f32_v1(&native) == 1,
              "native canonical f32 predicate failed");

        uint32_t round_trip = UINT32_C(0xdeadbeef);
        check(eshkol_value_f32_to_bits_v1(&native, &round_trip) ==
                  ESHKOL_VALUE_F32_OK &&
              round_trip == bits,
              "native f32 raw-bit round-trip failed");

        eshkol_ffi_value_t ffi;
        std::memset(&ffi, 0x5a, sizeof(ffi));
        check(eshkol_ffi_float32_from_bits_v1(bits, &ffi) == ESHKOL_FFI_F32_OK,
              "FFI f32 construction failed");
        check(std::memcmp(&native, &ffi, sizeof(native)) == 0,
              "native and FFI f32 values differ byte-for-byte");
        check(eshkol_ffi_is_float32_v1(&ffi) == 1,
              "FFI canonical f32 predicate failed");

        round_trip = UINT32_C(0xdeadbeef);
        check(eshkol_ffi_float32_to_bits_v1(&ffi, &round_trip) ==
                  ESHKOL_FFI_F32_OK &&
              round_trip == bits,
              "FFI f32 raw-bit round-trip failed");

        double promoted = -17.0;
        check(eshkol_value_f32_to_double_v1(&native, &promoted) ==
                  ESHKOL_VALUE_F32_OK,
              "native f32-to-double promotion failed");
        check(double_bits(promoted) == double_bits(expected_promotion(bits)),
              "native f32-to-double promotion bits mismatch");
        promoted = -17.0;
        check(eshkol_ffi_float32_to_double_v1(&ffi, &promoted) ==
                  ESHKOL_FFI_F32_OK,
              "FFI f32-to-double promotion failed");
        check(double_bits(promoted) == double_bits(expected_promotion(bits)),
              "FFI f32-to-double promotion bits mismatch");
    }
}

void test_rejection_and_output_preservation() {
    uint32_t bits = UINT32_C(0x11223344);
    check(eshkol_value_f32_from_bits_v1(nullptr, 0) ==
              ESHKOL_VALUE_F32_INVALID_ARGUMENT,
          "native null construction output was accepted");
    check(eshkol_value_f32_to_bits_v1(nullptr, &bits) ==
              ESHKOL_VALUE_F32_INVALID_ARGUMENT &&
          bits == UINT32_C(0x11223344),
          "native null input did not preserve output");

    eshkol_tagged_value_t good;
    check(eshkol_value_f32_from_bits_v1(&good, UINT32_C(0x3dcccccd)) == 0,
          "native rejection fixture construction failed");
    check(eshkol_value_f32_to_bits_v1(&good, nullptr) ==
              ESHKOL_VALUE_F32_INVALID_ARGUMENT,
          "native null bits output was accepted");
    check(eshkol_value_is_f32_v1(nullptr) == 0,
          "native null predicate returned true");
    double promoted = -17.0;
    check(eshkol_value_f32_to_double_v1(nullptr, &promoted) ==
              ESHKOL_VALUE_F32_INVALID_ARGUMENT &&
          double_bits(promoted) == double_bits(-17.0),
          "native null promotion input changed output");
    check(eshkol_value_f32_to_double_v1(&good, nullptr) ==
              ESHKOL_VALUE_F32_INVALID_ARGUMENT,
          "native null promotion output was accepted");

    eshkol_tagged_value_t malformed = good;
    malformed.type = ESHKOL_VALUE_DOUBLE;
    require_failed_native_inspection(malformed, "wrong f32 tag was accepted");
    malformed = good;
    malformed.flags = 0;
    require_failed_native_inspection(malformed, "wrong f32 flags were accepted");
    malformed = good;
    malformed.reserved = 1;
    require_failed_native_inspection(malformed, "nonzero f32 reserved field was accepted");
    malformed = good;
    reinterpret_cast<unsigned char*>(&malformed)[4] = 1;
    require_failed_native_inspection(malformed, "nonzero f32 implicit padding was accepted");
    malformed = good;
    malformed.data.raw_val |= UINT64_C(1) << 32;
    require_failed_native_inspection(malformed, "nonzero f32 upper payload was accepted");

    eshkol_ffi_value_t ffi;
    check(eshkol_ffi_float32_from_bits_v1(UINT32_C(0x3f800000), &ffi) == 0,
          "FFI rejection fixture construction failed");
    bits = UINT32_C(0x55667788);
    check(eshkol_ffi_float32_to_bits_v1(nullptr, &bits) ==
              ESHKOL_FFI_F32_INVALID_ARGUMENT &&
          bits == UINT32_C(0x55667788),
          "FFI null input did not preserve output");
    check(eshkol_ffi_float32_to_bits_v1(&ffi, nullptr) ==
              ESHKOL_FFI_F32_INVALID_ARGUMENT,
          "FFI null bits output was accepted");
    check(eshkol_ffi_is_float32_v1(nullptr) == 0,
          "FFI null predicate returned true");

    eshkol_ffi_value_t bad_ffi = ffi;
    bad_ffi.flags = 0;
    bits = UINT32_C(0x55667788);
    check(eshkol_ffi_float32_to_bits_v1(&bad_ffi, &bits) ==
              ESHKOL_FFI_F32_INVALID_VALUE &&
          bits == UINT32_C(0x55667788),
          "FFI malformed value changed raw-bit output");
    promoted = -17.0;
    check(eshkol_value_f32_to_double_v1(&malformed, &promoted) ==
              ESHKOL_VALUE_F32_INVALID_VALUE &&
          double_bits(promoted) == double_bits(-17.0),
          "native malformed value changed double output");
    promoted = -17.0;
    check(eshkol_ffi_float32_to_double_v1(&bad_ffi, &promoted) ==
              ESHKOL_FFI_F32_INVALID_VALUE &&
          double_bits(promoted) == double_bits(-17.0),
          "FFI malformed value changed double output");
    promoted = -17.0;
    check(eshkol_ffi_float32_to_double_v1(nullptr, &promoted) ==
              ESHKOL_FFI_F32_INVALID_ARGUMENT &&
          double_bits(promoted) == double_bits(-17.0),
          "FFI null promotion input changed output");
    check(eshkol_ffi_float32_to_double_v1(&ffi, nullptr) ==
              ESHKOL_FFI_F32_INVALID_ARGUMENT,
          "FFI null promotion output was accepted");

    eshkol_ffi_clear_error();
    check(eshkol_ffi_to_int64(ffi) == 0,
          "legacy by-value int accessor accepted f32 payload");
    check(eshkol_ffi_last_error() != nullptr,
          "legacy by-value int accessor did not diagnose f32");
    eshkol_ffi_clear_error();
    check(eshkol_ffi_to_double(ffi) == 0.0,
          "legacy by-value double accessor accepted f32 payload");
    check(eshkol_ffi_last_error() != nullptr,
          "legacy by-value double accessor did not diagnose f32");
    eshkol_ffi_clear_error();
    check(eshkol_ffi_to_bool(ffi) == 0,
          "legacy by-value boolean accessor accepted f32 payload");
    check(eshkol_ffi_last_error() != nullptr,
          "legacy by-value boolean accessor did not diagnose f32");
}

std::string display_value(const eshkol_tagged_value_t& value) {
    FILE* file = std::tmpfile();
    if (!file) return {};
    eshkol_display_opts_t opts = eshkol_display_default_opts();
    opts.output = file;
    eshkol_display_value_opts(&value, &opts);
    std::fflush(file);
    std::rewind(file);
    std::string output;
    char buffer[128];
    while (std::fgets(buffer, sizeof(buffer), file)) output += buffer;
    std::fclose(file);
    return output;
}

void test_core_value_semantics() {
    eshkol_tagged_value_t value{};
    check(eshkol_value_f32_from_bits_v1(&value, UINT32_C(0x3eaaaaab)) == 0,
          "core-semantics fixture construction failed");
    check(std::strcmp(eshkol_format_value_type_tag(value), "float32") == 0,
          "canonical f32 error type name mismatch");

    eshkol_tagged_value_t type = eshkol_type_of(value);
    check(type.type == ESHKOL_VALUE_HEAP_PTR && type.data.ptr_val != 0 &&
              std::strcmp(reinterpret_cast<const char*>(type.data.ptr_val), "float32") == 0,
          "type-of did not report float32");
    check(type.type == ESHKOL_VALUE_HEAP_PTR && type.data.ptr_val != 0 &&
              ESHKOL_GET_HEADER(reinterpret_cast<void*>(type.data.ptr_val))->subtype ==
                  HEAP_SUBTYPE_SYMBOL &&
              type.data.ptr_val == eshkol_type_of_ref_v1(&value).data.ptr_val,
          "public type-of did not delegate to the canonical runtime symbol helper");

    char expected[128];
    eshkol_format_float32_bits(expected, sizeof(expected), UINT32_C(0x3eaaaaab));
    check(display_value(value) == expected,
          "f32 display did not use the shared binary32 formatter");

    eshkol_tagged_value_t malformed = value;
    malformed.reserved = 1;
    check(std::strcmp(eshkol_format_value_type_tag(malformed),
                      "invalid-float32") == 0,
          "malformed f32 error type name mismatch");
    type = eshkol_type_of(malformed);
    check(type.type == ESHKOL_VALUE_HEAP_PTR && type.data.ptr_val != 0 &&
              std::strcmp(reinterpret_cast<const char*>(type.data.ptr_val), "unknown") == 0,
          "type-of admitted malformed f32");
    check(type.data.ptr_val == eshkol_type_of_ref_v1(&malformed).data.ptr_val,
          "public type-of malformed result diverged from runtime helper");
    check(display_value(malformed) == "#<invalid-float32>",
          "display admitted malformed f32");

    eshkol_tagged_value_t folded = value;
    folded.type = 27;
    check(std::strcmp(eshkol_format_value_type_tag(folded), "float32") != 0,
          "folded tag 27 reported as float32");
    folded.type = 43;
    check(std::strcmp(eshkol_format_value_type_tag(folded), "float32") != 0,
          "folded tag 43 reported as float32");
}

void test_float32_formatting() {
    struct FormatCase {
        uint32_t bits;
        const char* expected;
    };
    constexpr std::array<FormatCase, 12> cases = {{
        {UINT32_C(0x00000000), "0.0"},
        {UINT32_C(0x80000000), "-0.0"},
        {UINT32_C(0x3f800000), "1.0"},
        {UINT32_C(0x3fc00000), "1.5"},
        {UINT32_C(0x00000001), "1.401298464324817e-45"},
        {UINT32_C(0x007fffff), "1.1754942106924411e-38"},
        {UINT32_C(0x00800000), "1.1754943508222875e-38"},
        {UINT32_C(0x7f7fffff), "3.4028234663852886e+38"},
        {UINT32_C(0x7f800000), "+inf.0"},
        {UINT32_C(0xff800000), "-inf.0"},
        {UINT32_C(0x7fc12345), "+nan.0"},
        {UINT32_C(0xff812345), "+nan.0"},
    }};

    for (const auto& test : cases) {
        char text[64];
        eshkol_format_float32_bits(text, sizeof(text), test.bits);
        check(std::strcmp(text, test.expected) == 0,
              "shared f32 formatter output mismatch");

        eshkol_tagged_value_t value{};
        check(eshkol_value_f32_from_bits_v1(&value, test.bits) == 0,
              "format fixture construction failed");
        check(display_value(value) == test.expected,
              "native f32 display diverged from shared formatter");
    }

    eshkol_tagged_value_t value{};
    eshkol_value_f32_from_bits_v1(&value, UINT32_C(0x3f800000));
    struct FactWithArg {
        eshkol_fact_t fact;
        eshkol_tagged_value_t arg;
    } storage{};
    auto* fact = &storage.fact;
    fact->predicate = reinterpret_cast<uintptr_t>("metric");
    fact->arity = 1;
    *FACT_ARGS(fact) = value;
    FILE* file = std::tmpfile();
    check(file != nullptr, "logic formatting fixture stream creation failed");
    if (file) {
        eshkol_display_fact(fact, file);
        std::fflush(file);
        std::rewind(file);
        char text[64] = {};
        std::fgets(text, sizeof(text), file);
        std::fclose(file);
        check(std::strcmp(text, "(metric 1.0)") == 0,
              "logic f32 formatting diverged from shared formatter");
    }
}

void test_float32_error_rendering() {
#if !defined(_WIN32)
    int pipe_fds[2];
    const int pipe_status = pipe(pipe_fds);
    check(pipe_status == 0, "f32 error-rendering pipe creation failed");
    if (pipe_status != 0) return;
    const pid_t child = fork();
    check(child >= 0, "f32 error-rendering fork failed");
    if (child == 0) {
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);
        eshkol_ffi_pointer_arg_type_error(
            "consume-pointer", "consume_pointer", 1, "ptr",
            ESHKOL_VALUE_FLOAT32, UINT32_C(0x3f800000));
        _exit(99);
    }
    if (child < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return;
    }
    close(pipe_fds[1]);
    std::string error;
    char buf[256];
    ssize_t count;
    while ((count = read(pipe_fds[0], buf, sizeof(buf))) > 0)
        error.append(buf, static_cast<size_t>(count));
    close(pipe_fds[0]);
    int status = 0;
    waitpid(child, &status, 0);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 1,
          "f32 FFI type error did not terminate cleanly");
    check(error.find("the number 1.0") != std::string::npos,
          "f32 FFI type error did not use shared numeric rendering");
#endif
}

void test_copy_boundaries() {
    eshkol_tagged_value_t value;
    check(eshkol_value_f32_from_bits_v1(&value, UINT32_C(0x80000000)) == 0,
          "copy fixture construction failed");

    eshkol_tagged_value_t escaped;
    std::memset(&escaped, 0xa5, sizeof(escaped));
    check(eshkol_region_write_barrier_checked_v1(&escaped, nullptr, &value) == 0,
          "region barrier rejected canonical f32 immediate");
    check(std::memcmp(&escaped, &value, sizeof(value)) == 0,
          "region barrier changed canonical f32 bytes");

    arena_t* arena = arena_create(1024);
    check(arena != nullptr, "tagged-cons test arena allocation failed");
    if (!arena) return;
    arena_tagged_cons_cell_t* cell = arena_allocate_tagged_cons_cell(arena);
    check(cell != nullptr, "tagged-cons f32 cell allocation failed");
    if (cell) {
        arena_tagged_cons_set_tagged_value(cell, false, &value);
        check(std::memcmp(&cell->car, &value, sizeof(value)) == 0,
              "tagged-cons stored canonical f32 bytes changed");
        const eshkol_tagged_value_t copied =
            arena_tagged_cons_get_tagged_value(cell, false);
        check(copied.type == value.type && copied.flags == value.flags &&
                  copied.reserved == value.reserved &&
                  copied.data.raw_val == value.data.raw_val,
              "tagged-cons getter changed canonical f32 fields");

        // Transport is byte-preserving even for deliberately malformed F32
        // carriers.  Consumers reject these layouts; the cons cell must not
        // normalize, reinterpret, or drop any of their 16 ABI bytes.
        std::array<std::array<unsigned char, sizeof(value)>, 4> malformed{};
        for (auto& bytes : malformed) {
            std::memcpy(bytes.data(), &value, sizeof(value));
        }
        malformed[0][1] = 0;     // missing required inexact flag
        malformed[1][2] = 1;     // nonzero reserved field
        malformed[2][4] = 0xa1;  // nonzero implicit padding
        malformed[2][5] = 0xb2;
        malformed[2][6] = 0xc3;
        malformed[2][7] = 0xd4;
        malformed[3][12] = 0x5a; // nonzero upper payload word
        for (size_t i = 0; i < malformed.size(); ++i) {
            eshkol_tagged_value_t input;
            std::memcpy(&input, malformed[i].data(), sizeof(input));
            arena_tagged_cons_set_tagged_value(cell, (i & 1) != 0, &input);
            const eshkol_tagged_value_t* stored =
                (i & 1) != 0 ? &cell->cdr : &cell->car;
            check(std::memcmp(stored, malformed[i].data(), sizeof(input)) == 0,
                  "tagged-cons changed stored malformed f32 carrier bytes");
            check(eshkol_value_is_f32_v1(stored) == 0,
                  "stored malformed f32 passed canonical inspection");

            eshkol_tagged_value_t barrier_output;
            std::memset(&barrier_output, 0x3c, sizeof(barrier_output));
            check(eshkol_region_write_barrier_checked_v1(
                      &barrier_output, nullptr, &input) == 0,
                  "region barrier rejected malformed f32 immediate transport");
            std::array<unsigned char, sizeof(barrier_output)> barrier_bytes{};
            std::memcpy(barrier_bytes.data(), &barrier_output,
                        sizeof(barrier_output));
            check(barrier_bytes == malformed[i],
                  "region barrier changed malformed f32 carrier bytes");
        }

        arena_tagged_cons_set_tagged_value(cell, false, &value);
        arena_tagged_cons_set_int64(
            cell, false, 123,
            ESHKOL_VALUE_FLOAT32 | ESHKOL_VALUE_INEXACT_FLAG);
        const eshkol_tagged_value_t after_folded_set =
            arena_tagged_cons_get_tagged_value(cell, false);
        check(after_folded_set.type == value.type &&
                  after_folded_set.flags == value.flags &&
                  after_folded_set.reserved == value.reserved &&
                  after_folded_set.data.raw_val == value.data.raw_val,
              "folded f32 tag was accepted as int storage");
    }
    arena_destroy(arena);

    // Use a real header-backed pointer in an active inner region. If tag 11 is
    // ever misclassified as pointer-carrying, the barrier will evacuate this
    // string into the outer region and change the payload.
    eshkol_region_t* outer = region_create("f32-outer", 1024);
    check(outer != nullptr, "outer f32 region allocation failed");
    if (!outer) return;
    region_push(outer);
    void* outer_dst = arena_allocate(outer->arena, sizeof(value));
    check(outer_dst != nullptr, "outer f32 destination allocation failed");

    eshkol_region_t* inner = region_create("f32-inner", 1024);
    check(inner != nullptr, "inner f32 region allocation failed");
    if (inner) {
        region_push(inner);
        char* inner_string = arena_allocate_string_with_header(inner->arena, 3);
        check(inner_string != nullptr, "inner f32 pointer fixture allocation failed");
        if (inner_string) {
            std::memcpy(inner_string, "f32", 4);
            eshkol_tagged_value_t pointer_shaped = value;
            pointer_shaped.data.ptr_val = reinterpret_cast<uintptr_t>(inner_string);
            check(eshkol_value_is_f32_v1(&pointer_shaped) == 0,
                  "pointer-shaped noncanonical f32 passed canonical inspection");
            std::memset(&escaped, 0, sizeof(escaped));
            check(eshkol_region_write_barrier_checked_v1(
                      &escaped, outer_dst, &pointer_shaped) == 0,
                  "region barrier rejected pointer-shaped f32 immediate");
            check(std::memcmp(&escaped, &pointer_shaped,
                              sizeof(pointer_shaped)) == 0,
                  "region barrier evacuated pointer-shaped f32 payload");
            check(inner->escape_count == 0,
                  "region barrier counted a pointer-shaped f32 escape");
        }
        region_pop();
    }
    region_pop();
}

using RejectionCall = void (*)(void*);

void expect_runtime_rejection(RejectionCall call, void* context,
                              const char* message) {
    jmp_buf handler;
    volatile int transferred = 0;
    eshkol_push_exception_handler(&handler);
    if (setjmp(handler) == 0) {
        call(context);
    } else {
        transferred = 1;
    }
    eshkol_pop_exception_handler();
    const bool caught = transferred == 1 && g_current_exception != nullptr;
    eshkol_clear_current_exception();
    check(caught, message);
}

struct UnaryContext {
    arena_t* arena;
    const eshkol_tagged_value_t* value;
};

void reject_index(void* opaque) {
    auto* context = static_cast<UnaryContext*>(opaque);
    (void)eshkol_unwrap_list_index(context->value);
}

void reject_tensor(void* opaque) {
    auto* context = static_cast<UnaryContext*>(opaque);
    (void)eshkol_tensor_from_collection(context->arena, context->value);
}

void reject_ad(void* opaque) {
    auto* context = static_cast<UnaryContext*>(opaque);
    (void)eshkol_ad_point_to_double(context->value, "f32-test");
}

void reject_ad_list(void* opaque) {
    auto* context = static_cast<UnaryContext*>(opaque);
    (void)eshkol_list_to_svec(context->arena, context->value);
}

void reject_ad_extract(void* opaque) {
    auto* context = static_cast<UnaryContext*>(opaque);
    double output = -17.0;
    (void)eshkol_ad_extract_doubles(context->value, &output, 1);
}

void reject_bignum_arithmetic(void* opaque) {
    auto* context = static_cast<UnaryContext*>(opaque);
    eshkol_tagged_value_t integer = eshkol_make_int64(2, true);
    eshkol_tagged_value_t result{};
    eshkol_bignum_binary_tagged(
        context->arena, context->value, &integer, 0, &result);
}

void reject_gcd(void* opaque) {
    auto* context = static_cast<UnaryContext*>(opaque);
    eshkol_tagged_value_t integer = eshkol_make_int64(2, true);
    eshkol_tagged_value_t result{};
    eshkol_gcd_tagged(
        context->arena, context->value, &integer, &result);
}

void reject_rational_arithmetic(void* opaque) {
    auto* context = static_cast<UnaryContext*>(opaque);
    eshkol_tagged_value_t integer = eshkol_make_int64(2, true);
    (void)eshkol_rational_binary_tagged(
        context->arena, *context->value, integer, 0);
}

void reject_rational_numerator(void* opaque) {
    auto* context = static_cast<UnaryContext*>(opaque);
    eshkol_tagged_value_t result{};
    eshkol_rational_numerator_tagged(
        context->arena, context->value, &result);
}

void reject_rational_denominator(void* opaque) {
    auto* context = static_cast<UnaryContext*>(opaque);
    eshkol_tagged_value_t result{};
    eshkol_rational_denominator_tagged(
        context->arena, context->value, &result);
}

void test_f32_numerator() {
    arena_t* arena = arena_create(1024);
    check(arena != nullptr, "numerator arena allocation failed");
    if (!arena) return;
    constexpr std::array<uint32_t, 9> patterns = {
        UINT32_C(0x00000000), UINT32_C(0x80000000),
        UINT32_C(0x00000001), UINT32_C(0x80000001),
        UINT32_C(0x3fc00000), UINT32_C(0xbfc00000),
        UINT32_C(0x7f800000), UINT32_C(0xff800000),
        UINT32_C(0xff812345),
    };
    for (uint32_t bits : patterns) {
        eshkol_tagged_value_t value{}, result{};
        check(eshkol_value_f32_from_bits_v1(&value, bits) ==
                  ESHKOL_VALUE_F32_OK,
              "numerator F32 fixture construction failed");
        eshkol_rational_numerator_tagged(arena, &value, &result);
        check(result.type == ESHKOL_VALUE_DOUBLE &&
                  result.flags == ESHKOL_VALUE_INEXACT_FLAG &&
                  double_bits(result.data.double_val) ==
                      double_bits(expected_promotion(bits)),
              "numerator F32 did not return promoted DOUBLE bits");
    }
    eshkol_tagged_value_t integer = eshkol_make_int64(3, true);
    eshkol_tagged_value_t real = eshkol_make_double(-0.0);
    for (const eshkol_tagged_value_t* value : {&integer, &real}) {
        eshkol_tagged_value_t result{};
        eshkol_rational_numerator_tagged(arena, value, &result);
        check(result.type == value->type && result.flags == value->flags &&
                  result.data.raw_val == value->data.raw_val,
              "numerator non-F32 identity control changed");
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
    for (const eshkol_tagged_value_t& value : invalid) {
        UnaryContext context{arena, &value};
        expect_runtime_rejection(reject_rational_numerator, &context,
                                 "numerator accepted malformed/folded F32");
    }
    arena_destroy(arena);
}

void test_f32_denominator() {
    arena_t* arena = arena_create(1024);
    check(arena != nullptr, "denominator arena allocation failed");
    if (!arena) return;
    constexpr std::array<uint32_t, 9> patterns = {
        UINT32_C(0x00000000), UINT32_C(0x80000000),
        UINT32_C(0x00000001), UINT32_C(0x80000001),
        UINT32_C(0x3fc00000), UINT32_C(0xbfc00000),
        UINT32_C(0x7f800000), UINT32_C(0xff800000),
        UINT32_C(0xff812345),
    };
    for (uint32_t bits : patterns) {
        eshkol_tagged_value_t value{}, result{};
        check(eshkol_value_f32_from_bits_v1(&value, bits) ==
                  ESHKOL_VALUE_F32_OK,
              "denominator F32 fixture construction failed");
        eshkol_rational_denominator_tagged(arena, &value, &result);
        check(result.type == ESHKOL_VALUE_INT64 && result.data.int_val == 1,
              "denominator F32 did not follow DOUBLE result path");
    }
    eshkol_tagged_value_t integer = eshkol_make_int64(3, true);
    eshkol_tagged_value_t real = eshkol_make_double(-2.5);
    for (const eshkol_tagged_value_t* value : {&integer, &real}) {
        eshkol_tagged_value_t result{};
        eshkol_rational_denominator_tagged(arena, value, &result);
        check(result.type == ESHKOL_VALUE_INT64 && result.data.int_val == 1,
              "denominator non-F32 control changed");
    }
    eshkol_tagged_value_t malformed{};
    (void)eshkol_value_f32_from_bits_v1(&malformed, UINT32_C(0x3fc00000));
    malformed.flags = 0;
    UnaryContext bad{arena, &malformed};
    expect_runtime_rejection(reject_rational_denominator, &bad,
                             "denominator accepted malformed F32");
    arena_destroy(arena);
}

struct RationalAliasContext {
    arena_t* arena;
    eshkol_tagged_value_t value;
};

void reject_rational_compare_alias(void* opaque) {
    auto* context = static_cast<RationalAliasContext*>(opaque);
    eshkol_tagged_value_t integer = eshkol_make_int64(2, true);
    eshkol_rational_compare_tagged_ptr(
        context->arena, &context->value, &integer, 2, &context->value);
}

void reject_rationalize_alias(void* opaque) {
    auto* context = static_cast<RationalAliasContext*>(opaque);
    eshkol_tagged_value_t epsilon = eshkol_make_double(0.1);
    eshkol_rationalize_tagged(
        context->arena, &context->value, &epsilon, &context->value);
}

void reject_taylor_c0(void* opaque) {
    auto* context = static_cast<UnaryContext*>(opaque);
    (void)eshkol_taylor_c0(context->value);
}

void reject_taylor_seed(void* opaque) {
    auto* context = static_cast<UnaryContext*>(opaque);
    eshkol_tagged_value_t result{};
    eshkol_taylor_seed_tagged(context->arena, context->value, 1, &result);
}

void reject_taylor_unary(void* opaque) {
    auto* context = static_cast<UnaryContext*>(opaque);
    eshkol_tagged_value_t result{};
    eshkol_taylor_unary_tagged(context->arena, context->value, 0, &result);
}

void reject_taylor_binary(void* opaque) {
    auto* context = static_cast<UnaryContext*>(opaque);
    eshkol_tagged_value_t integer = eshkol_make_int64(2, true);
    eshkol_tagged_value_t result{};
    eshkol_taylor_binary_tagged(
        context->arena, context->value, &integer, 0, &result);
}

void reject_taylor_extract(void* opaque) {
    auto* context = static_cast<UnaryContext*>(opaque);
    (void)eshkol_taylor_extract(context->value, 0);
}

struct TapeContext {
    eshkol_tagged_value_t tape;
    const eshkol_tagged_value_t* value;
};

void reject_tape_const(void* opaque) {
    auto* context = static_cast<TapeContext*>(opaque);
    eshkol_tagged_value_t result{};
    eshkol_ad_const_sret(&result, &context->tape, context->value);
}

void reject_tape_var(void* opaque) {
    auto* context = static_cast<TapeContext*>(opaque);
    eshkol_tagged_value_t result{};
    eshkol_ad_var_sret(&result, &context->tape, context->value);
}

void reject_dnc_scalar(void* opaque) {
    auto* context = static_cast<UnaryContext*>(opaque);
    eshkol_tagged_value_t beta = eshkol_make_double(1.0);
    eshkol_tagged_value_t count = eshkol_make_int64(4, true);
    eshkol_tagged_value_t result{};
    eshkol_dnc_loc_address_tagged(
        context->arena, context->value, &beta, &count, &result);
}

struct SdncContext {
    arena_t* arena;
    eshkol_tagged_value_t theta;
    eshkol_tagged_value_t input;
};

void reject_sdnc_vector(void* opaque) {
    auto* context = static_cast<SdncContext*>(opaque);
    eshkol_tagged_value_t result{};
    eshkol_sdnc_run_tagged(
        context->arena, &context->theta, &context->input, &result);
}

void test_unsupported_generic_paths_reject() {
    eshkol_tagged_value_t value;
    check(eshkol_value_f32_from_bits_v1(&value, UINT32_C(0x3fc00000)) == 0,
          "unsupported-path fixture construction failed");
    check(get_global_arena_shared() != nullptr,
          "runtime root arena initialization failed");
    __repl_shared_arena.store(get_global_arena_shared());

    arena_t* arena = arena_create(1024);
    check(arena != nullptr, "unsupported-path arena allocation failed");
    if (!arena) return;

    UnaryContext unary{arena, &value};
    expect_runtime_rejection(reject_index, &unary,
                             "tensor index accepted f32");
    arena_block_t* tensor_block = arena->current_block;
    const size_t before_tensor_rejection = tensor_block ? tensor_block->used : 0;
    expect_runtime_rejection(reject_tensor, &unary,
                             "tensor construction accepted f32");
    check(arena->current_block == tensor_block &&
              (!tensor_block || tensor_block->used == before_tensor_rejection),
          "tensor f32 rejection allocated before failure");
    check(eshkol_ad_point_is_scalar(&value) == 1,
          "AD classified f32 as a collection");
    expect_runtime_rejection(reject_ad, &unary,
                             "AD accepted f32");
    expect_runtime_rejection(reject_bignum_arithmetic, &unary,
                             "bignum arithmetic accepted f32");
    expect_runtime_rejection(reject_gcd, &unary,
                             "gcd accepted f32");
    expect_runtime_rejection(reject_rational_arithmetic, &unary,
                             "rational arithmetic accepted f32");
    RationalAliasContext compare_alias{};
    compare_alias.arena = arena;
    std::memcpy(&compare_alias.value, &value, sizeof(value));
    std::array<unsigned char, sizeof(value)> compare_before{};
    std::memcpy(compare_before.data(), &compare_alias.value, sizeof(value));
    expect_runtime_rejection(reject_rational_compare_alias, &compare_alias,
                             "aliased rational comparison accepted f32");
    check(std::memcmp(&compare_alias.value, compare_before.data(),
                      sizeof(value)) == 0,
          "rational comparison changed aliased f32 before rejection");
    RationalAliasContext rationalize_alias{};
    rationalize_alias.arena = arena;
    std::memcpy(&rationalize_alias.value, &value, sizeof(value));
    std::array<unsigned char, sizeof(value)> rationalize_before{};
    std::memcpy(rationalize_before.data(), &rationalize_alias.value,
                sizeof(value));
    expect_runtime_rejection(reject_rationalize_alias, &rationalize_alias,
                             "aliased rationalize accepted f32");
    check(std::memcmp(&rationalize_alias.value, rationalize_before.data(),
                      sizeof(value)) == 0,
          "rationalize changed aliased f32 before rejection");
    expect_runtime_rejection(reject_taylor_c0, &unary,
                             "Taylor c0 accepted f32");
    expect_runtime_rejection(reject_taylor_seed, &unary,
                             "Taylor seed accepted f32");
    expect_runtime_rejection(reject_taylor_unary, &unary,
                             "Taylor unary operation accepted f32");
    expect_runtime_rejection(reject_taylor_binary, &unary,
                             "Taylor binary operation accepted f32");
    expect_runtime_rejection(reject_taylor_extract, &unary,
                             "Taylor extraction accepted f32");

    TapeContext tape{};
    tape.value = &value;
    eshkol_ad_tape_new_sret(&tape.tape);
    check(tape.tape.type == ESHKOL_VALUE_HEAP_PTR,
          "AD tape rejection fixture construction failed");
    if (tape.tape.type == ESHKOL_VALUE_HEAP_PTR) {
        expect_runtime_rejection(reject_tape_const, &tape,
                                 "AD tape constant accepted f32");
        expect_runtime_rejection(reject_tape_var, &tape,
                                 "AD tape variable accepted f32");
        eshkol_tagged_value_t released{};
        eshkol_ad_tape_release_sret(&released, &tape.tape);
    }
    expect_runtime_rejection(reject_dnc_scalar, &unary,
                             "DNC scalar conversion accepted f32");

    void* vector = arena_allocate_vector_with_header(arena, 1);
    check(vector != nullptr, "unsupported vector fixture allocation failed");
    if (!vector) {
        arena_destroy(arena);
        return;
    }
    *static_cast<int64_t*>(vector) = 1;
    *reinterpret_cast<eshkol_tagged_value_t*>(
        static_cast<char*>(vector) + sizeof(int64_t)) = value;
    eshkol_tagged_value_t vector_value{};
    vector_value.type = ESHKOL_VALUE_HEAP_PTR;
    vector_value.data.ptr_val = reinterpret_cast<uintptr_t>(vector);

    arena_tagged_cons_cell_t* cell = arena_allocate_tagged_cons_cell(arena);
    check(cell != nullptr, "unsupported AD list fixture allocation failed");
    if (cell) {
        arena_tagged_cons_set_tagged_value(cell, false, &value);
        eshkol_tagged_value_t list{};
        list.type = ESHKOL_VALUE_CONS_PTR;
        list.data.ptr_val = reinterpret_cast<uintptr_t>(cell);
        UnaryContext list_context{arena, &list};
        arena_block_t* list_block = arena->current_block;
        const size_t before_list_rejection = list_block ? list_block->used : 0;
        expect_runtime_rejection(reject_ad_list, &list_context,
                                 "AD list coercion accepted f32");
        check(arena->current_block == list_block &&
                  (!list_block || list_block->used == before_list_rejection),
              "AD f32 list rejection allocated before failure");
        expect_runtime_rejection(reject_ad_extract, &list_context,
                                 "AD list extraction accepted f32");
    }

    eshkol_tagged_value_t one = eshkol_make_int64(1, true);
    eshkol_tagged_value_t graph{};
    eshkol_make_factor_graph_tagged(arena, &one, &vector_value, &graph);
    check(graph.type == ESHKOL_VALUE_NULL,
          "inference vector conversion accepted f32");

    eshkol_tagged_value_t name{};
    eshkol_tagged_value_t theta{};
    eshkol_sdnc_program_tagged(arena, &name, &theta);
    check(theta.type == ESHKOL_VALUE_HEAP_PTR,
          "SDNC rejection fixture construction failed");
    if (theta.type == ESHKOL_VALUE_HEAP_PTR) {
        SdncContext sdnc{arena, theta, vector_value};
        expect_runtime_rejection(reject_sdnc_vector, &sdnc,
                                 "SDNC vector conversion accepted f32");
        // SdncHandle's first field is its separately calloc-owned weight set.
        // The production handle has no destructor API yet, so release the
        // fixture ownership explicitly before destroying its arena storage.
        std::free(*reinterpret_cast<void**>(theta.data.ptr_val));
    }
    arena_destroy(arena);
}

}  // namespace

int main() {
    test_layout_and_round_trip();
    test_rejection_and_output_preservation();
    test_core_value_semantics();
    test_float32_formatting();
    test_float32_error_rendering();
    test_copy_boundaries();
    test_unsupported_generic_paths_reject();
    test_f32_numerator();
    test_f32_denominator();
    if (failures != 0) {
        std::cerr << "f32_scalar_abi_test: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "f32_scalar_abi_test: PASS\n";
    return 0;
}
