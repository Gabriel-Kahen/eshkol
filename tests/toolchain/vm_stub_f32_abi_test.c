/*
 * Copyright (C) tsotchke
 * SPDX-License-Identifier: MIT
 */

#include <eshkol/backend/vm.h>

#include <stdint.h>
#include <stdio.h>

#if ESHKOL_VM_HAS_F32_HOST_TRANSPORT_V1 != 0
#error VM stub profile must not advertise f32 host transport
#endif

int main(void) {
    uint32_t output = UINT32_C(0x12345678);
    if (eshkol_vm_host_pop_float32_bits_v1(NULL, &output) !=
            ESHKOL_VM_F32_STATUS_INVALID_ARGUMENT ||
        output != UINT32_C(0x12345678)) {
        fputs("FAIL: VM stub f32 pop contract\n", stderr);
        return 1;
    }
    if (eshkol_vm_host_push_float32_bits_v1(
            NULL, UINT32_C(0x3f800000)) !=
            ESHKOL_VM_F32_STATUS_INVALID_ARGUMENT) {
        fputs("FAIL: VM stub f32 push contract\n", stderr);
        return 1;
    }
    puts("PASS: VM stub reports unavailable and exports rejecting f32 calls");
    return 0;
}
