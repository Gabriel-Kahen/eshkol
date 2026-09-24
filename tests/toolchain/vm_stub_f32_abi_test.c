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

_Static_assert(sizeof(EshkolVmFloat32StatusV1) == sizeof(int32_t),
               "f32 status ABI must be int32_t");
_Static_assert(ESHKOL_VM_F32_OK == 0, "f32 OK status changed");
_Static_assert(ESHKOL_VM_F32_INVALID_ARGUMENT == 1,
               "f32 invalid-argument status changed");
_Static_assert(ESHKOL_VM_F32_STACK_UNDERFLOW == 2,
               "f32 stack-underflow status changed");
_Static_assert(ESHKOL_VM_F32_WRONG_TYPE == 3,
               "f32 wrong-type status changed");
_Static_assert(ESHKOL_VM_F32_STACK_OVERFLOW == 4,
               "f32 stack-overflow status changed");

int main(void) {
    uint32_t output = UINT32_C(0x12345678);
    if (eshkol_vm_host_pop_float32_bits_v1(NULL, &output) !=
            ESHKOL_VM_F32_INVALID_ARGUMENT ||
        output != UINT32_C(0x12345678)) {
        fputs("FAIL: VM stub f32 pop contract\n", stderr);
        return 1;
    }
    if (eshkol_vm_host_push_float32_bits_v1(
            NULL, UINT32_C(0x3f800000)) !=
            ESHKOL_VM_F32_INVALID_ARGUMENT) {
        fputs("FAIL: VM stub f32 push contract\n", stderr);
        return 1;
    }
    puts("PASS: VM stub reports unavailable and exports rejecting f32 calls");
    return 0;
}
