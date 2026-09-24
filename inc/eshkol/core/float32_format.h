/*
 * Copyright (C) tsotchke
 * SPDX-License-Identifier: MIT
 *
 * Shared binary32 scalar formatting for native and VM runtimes.
 */
#ifndef ESHKOL_CORE_FLOAT32_FORMAT_H
#define ESHKOL_CORE_FLOAT32_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <eshkol/core/dtoa_shortest.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Format raw IEEE-754 binary32 bits through the exact widened-binary64
 * contract. Finite integral results retain an inexact decimal marker.
 */
static int eshkol_format_float32_bits_shared(
    char* buf, size_t cap, uint32_t bits) {
    const uint32_t magnitude = bits & UINT32_C(0x7fffffff);
    if (magnitude == 0) {
        return snprintf(buf, cap,
                        (bits & UINT32_C(0x80000000)) ? "-0.0" : "0.0");
    }

    float value;
    memcpy(&value, &bits, sizeof(value));
    const double widened = (double)value;

    char tmp[64];
    int written = eshkol_dtoa_shortest(tmp, sizeof(tmp), widened);
    if ((bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000) &&
        !strchr(tmp, '.') && !strchr(tmp, 'e') && !strchr(tmp, 'E')) {
        if ((size_t)written + 2 < sizeof(tmp)) {
            tmp[written++] = '.';
            tmp[written++] = '0';
            tmp[written] = '\0';
        }
    }
    return snprintf(buf, cap, "%s", tmp);
}

#ifdef __cplusplus
}
#endif

#endif /* ESHKOL_CORE_FLOAT32_FORMAT_H */
