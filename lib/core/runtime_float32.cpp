/*
 * Copyright (C) tsotchke
 *
 * SPDX-License-Identifier: MIT
 *
 * Versioned true-binary32 scalar representation helpers.
 */

#include <eshkol/eshkol.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

bool has_zero_implicit_padding(const eshkol_tagged_value_t* value) {
    unsigned char bytes[sizeof(*value)];
    std::memcpy(bytes, value, sizeof(bytes));
    for (std::size_t i = sizeof(value->type) + sizeof(value->flags) +
                         sizeof(value->reserved);
         i < offsetof(eshkol_tagged_value_t, data); ++i) {
        if (bytes[i] != 0) return false;
    }
    return true;
}

bool is_canonical_f32(const eshkol_tagged_value_t* value) {
    return value != nullptr &&
           value->type == ESHKOL_VALUE_FLOAT32 &&
           value->flags == ESHKOL_VALUE_INEXACT_FLAG &&
           value->reserved == 0 &&
           has_zero_implicit_padding(value) &&
           (value->data.raw_val >> 32) == 0;
}

}  // namespace

extern "C" uint32_t eshkol_runtime_has_f32_scalar_v1(void) {
    return 1;
}

extern "C" int32_t eshkol_value_f32_from_bits_v1(
    eshkol_tagged_value_t* out, uint32_t bits) {
    if (!out) return ESHKOL_VALUE_F32_INVALID_ARGUMENT;

    eshkol_tagged_value_t value;
    std::memset(&value, 0, sizeof(value));
    value.type = ESHKOL_VALUE_FLOAT32;
    value.flags = ESHKOL_VALUE_INEXACT_FLAG;
    value.data.raw_val = static_cast<uint64_t>(bits);
    std::memcpy(out, &value, sizeof(value));
    return ESHKOL_VALUE_F32_OK;
}

extern "C" int32_t eshkol_value_f32_to_bits_v1(
    const eshkol_tagged_value_t* value, uint32_t* out_bits) {
    if (!value || !out_bits) return ESHKOL_VALUE_F32_INVALID_ARGUMENT;
    if (!is_canonical_f32(value)) return ESHKOL_VALUE_F32_INVALID_VALUE;

    const uint32_t bits = static_cast<uint32_t>(value->data.raw_val);
    std::memcpy(out_bits, &bits, sizeof(bits));
    return ESHKOL_VALUE_F32_OK;
}

extern "C" int32_t eshkol_value_is_f32_v1(
    const eshkol_tagged_value_t* value) {
    return is_canonical_f32(value) ? 1 : 0;
}

extern "C" int32_t eshkol_value_f32_to_double_v1(
    const eshkol_tagged_value_t* value, double* out) {
    if (!value || !out) return ESHKOL_VALUE_F32_INVALID_ARGUMENT;

    uint32_t bits = 0;
    const int32_t status = eshkol_value_f32_to_bits_v1(value, &bits);
    if (status != ESHKOL_VALUE_F32_OK) return status;

    double promoted;
    if ((bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) &&
        (bits & UINT32_C(0x007fffff)) != 0) {
        const uint64_t canonical_nan = UINT64_C(0x7ff8000000000000);
        std::memcpy(&promoted, &canonical_nan, sizeof(promoted));
    } else {
        static_assert(sizeof(float) == sizeof(bits),
                      "binary32 conversion requires a 32-bit float");
        static_assert(std::numeric_limits<float>::is_iec559 &&
                          std::numeric_limits<float>::digits == 24,
                      "binary32 conversion requires IEEE-754 float");
        static_assert(sizeof(double) == sizeof(uint64_t) &&
                          std::numeric_limits<double>::is_iec559 &&
                          std::numeric_limits<double>::digits == 53,
                      "binary32 promotion requires IEEE-754 binary64 double");
        float source;
        std::memcpy(&source, &bits, sizeof(source));
        promoted = static_cast<double>(source);
    }
    std::memcpy(out, &promoted, sizeof(promoted));
    return ESHKOL_VALUE_F32_OK;
}
