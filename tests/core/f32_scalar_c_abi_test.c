/*
 * Copyright (C) tsotchke
 *
 * SPDX-License-Identifier: MIT
 */

#include <eshkol/eshkol.h>
#include <eshkol/eshkol_ffi.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fail(const char* message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void) {
    eshkol_tagged_value_t value;
    uint32_t bits = 0;
    double promoted = 0.0;
    uint64_t promoted_bits = 0;

    if (ESHKOL_HAS_F32_SCALAR_ABI_V1 != 1 ||
        eshkol_runtime_has_f32_scalar_v1() != 1) {
        return fail("f32 feature probe unavailable");
    }
    if (eshkol_value_f32_from_bits_v1(&value, UINT32_C(0x3fc00000)) !=
        ESHKOL_VALUE_F32_OK) {
        return fail("C construction failed");
    }
    if (!eshkol_value_is_f32_v1(&value) ||
        eshkol_value_f32_to_bits_v1(&value, &bits) != ESHKOL_VALUE_F32_OK ||
        bits != UINT32_C(0x3fc00000)) {
        return fail("C raw-bit inspection failed");
    }
    if (eshkol_value_f32_to_double_v1(&value, &promoted) !=
        ESHKOL_VALUE_F32_OK) {
        return fail("C promotion failed");
    }
    memcpy(&promoted_bits, &promoted, sizeof(promoted_bits));
    if (promoted_bits != UINT64_C(0x3ff8000000000000)) {
        return fail("C promotion produced wrong binary64 bits");
    }

    puts("f32_scalar_c_abi_test: PASS");
    return 0;
}
