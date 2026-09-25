/*
 * Copyright (C) tsotchke
 *
 * SPDX-License-Identifier: MIT
 *
 * Runtime closure allocation helpers.
 *
 * These helpers implement the closure object/environment allocation ABI used
 * by generated closure code, higher-order functions, and continuation setup.
 * They sit above raw arena block allocation but do not depend on hosted
 * process, filesystem, or thread APIs.
 */

#include "arena_memory.h"
#include "../../inc/eshkol/logger.h"

#include <limits>

namespace {

constexpr size_t kClosureAllocationAlignment = 16;

bool checked_add(size_t lhs, size_t rhs, size_t* result) {
    if (rhs > std::numeric_limits<size_t>::max() - lhs) return false;
    *result = lhs + rhs;
    return true;
}

bool checked_capture_storage_size(size_t num_captures, size_t* result) {
    if (num_captures >
        (std::numeric_limits<size_t>::max() - sizeof(eshkol_closure_env_t)) /
            sizeof(eshkol_tagged_value_t)) {
        return false;
    }
    *result = sizeof(eshkol_closure_env_t) +
              num_captures * sizeof(eshkol_tagged_value_t);
    return true;
}

bool checked_align_up(size_t value, size_t alignment, size_t* result) {
    const size_t mask = alignment - 1;
    if (value > std::numeric_limits<size_t>::max() - mask) return false;
    *result = (value + mask) & ~mask;
    return true;
}

void initialize_capture_environment(eshkol_closure_env_t* env,
                                    size_t packed_info,
                                    size_t num_captures) {
    env->num_captures = packed_info;
    for (size_t i = 0; i < num_captures; ++i) {
        env->captures[i].type = ESHKOL_VALUE_NULL;
        env->captures[i].flags = 0;
        env->captures[i].reserved = 0;
        env->captures[i].data.raw_val = 0;
    }
}

void initialize_closure(eshkol_closure_t* closure, uint64_t func_ptr,
                        size_t packed_info, uint64_t sexpr_ptr,
                        uint64_t return_type_info, const char* name) {
    closure->func_ptr = func_ptr;
    closure->sexpr_ptr = sexpr_ptr;
    closure->name = name;
    closure->return_type = (uint8_t)(return_type_info & 0xFF);
    closure->input_arity = (uint8_t)((return_type_info >> 8) & 0xFF);
    closure->flags = CLOSURE_ENV_IS_VARIADIC(packed_info) ?
        CLOSURE_FLAG_VARIADIC : 0;
    if (name) closure->flags |= ESHKOL_CLOSURE_FLAG_NAMED;
    closure->reserved = 0;
    closure->hott_type_id = (uint32_t)((return_type_info >> 16) & 0xFFFFFFFF);
}

}  // namespace

extern "C" {

/**
 * @brief Allocate a closure environment (captured-variable array) in the arena.
 *
 * Allocates `sizeof(eshkol_closure_env_t) + num_captures * sizeof(eshkol_tagged_value_t)`
 * bytes, 16-byte aligned, and zero-initializes every capture slot to a null
 * tagged value. The `num_captures` field of the returned environment is set
 * to the raw count here; callers that need the packed
 * capture/arity/variadic encoding (see CLOSURE_ENV_PACK) overwrite it
 * afterward. The environment is arena-owned: it is freed only when the
 * arena itself is freed/reset, never individually.
 *
 * @param arena         Arena to allocate from (must not be null).
 * @param num_captures  Number of captured tagged values to reserve space for.
 * @return              Newly allocated environment, or nullptr on failure.
 */
eshkol_closure_env_t* arena_allocate_closure_env(arena_t* arena, size_t num_captures) {
    if (!arena) {
        eshkol_error("Cannot allocate closure environment: null arena");
        return nullptr;
    }

    if (num_captures == 0) {
        eshkol_warn("Allocating closure environment with zero captures");
    }

    size_t size = 0;
    if (!checked_capture_storage_size(num_captures, &size)) {
        eshkol_error("Closure environment allocation size overflow");
        return nullptr;
    }

    auto* env = (eshkol_closure_env_t*)arena_allocate_aligned(arena, size, 16);
    if (!env) {
        eshkol_error("Failed to allocate closure environment for %zu captures", num_captures);
        return nullptr;
    }

    env->num_captures = num_captures;

    for (size_t i = 0; i < num_captures; i++) {
        env->captures[i].type = ESHKOL_VALUE_NULL;
        env->captures[i].flags = 0;
        env->captures[i].reserved = 0;
        env->captures[i].data.raw_val = 0;
    }

    eshkol_debug("Allocated closure environment for %zu captures at %p",
                 num_captures, (void*)env);

    return env;
}

/**
 * @brief Allocate a plain (headerless) closure object in the arena.
 *
 * Allocates an `eshkol_closure_t`, 16-byte aligned, and fills it in from the
 * caller-supplied function pointer, packed capture/arity/variadic info
 * (unpacked via CLOSURE_ENV_GET_NUM_CAPTURES / CLOSURE_ENV_IS_VARIADIC),
 * S-expression pointer (for homoiconicity), and return-type metadata packed
 * into `return_type_info` (low byte = return type, next byte = input arity,
 * upper 32 bits = HoTT type id). If `packed_info` encodes at least one
 * capture, reserves a 16-byte-aligned closure environment in the same arena
 * allocation and stores the *packed* info (not just the capture count) into
 * `env->num_captures` so downstream code can recover arity/variadic flags from
 * the environment alone. The single allocation is failure-atomic; the closure
 * (and its environment, if any) are arena-owned and live until reset/free.
 *
 * @param arena             Arena to allocate from (must not be null).
 * @param func_ptr          Pointer to the generated lambda function.
 * @param packed_info       Packed capture count / fixed-param count / variadic bit
 *                          (see CLOSURE_ENV_PACK).
 * @param sexpr_ptr         Pointer to the closure's S-expression form, or 0.
 * @param return_type_info  Packed return-type byte, arity byte, and HoTT type id.
 * @param name              Bound name for a named closure, or nullptr for anonymous.
 * @return                  Newly allocated closure, or nullptr on failure.
 */
eshkol_closure_t* arena_allocate_closure(arena_t* arena, uint64_t func_ptr, uint64_t packed_info,
                                         uint64_t sexpr_ptr, uint64_t return_type_info,
                                         const char* name) {
    if (!arena) {
        eshkol_error("Cannot allocate closure: null arena");
        return nullptr;
    }

    const size_t actual_num_captures = CLOSURE_ENV_GET_NUM_CAPTURES(packed_info);

    size_t total = sizeof(eshkol_closure_t);
    size_t env_offset = 0;
    if (actual_num_captures > 0) {
        size_t env_size = 0;
        if (!checked_align_up(total, kClosureAllocationAlignment, &env_offset) ||
            !checked_capture_storage_size(actual_num_captures, &env_size) ||
            !checked_add(env_offset, env_size, &total)) {
            eshkol_error("Closure allocation size overflow");
            return nullptr;
        }
    }

    // The closure and capture environment are one commit.  A caught allocation
    // failure therefore cannot retain a partial closure in the bump arena.
    auto* storage = (uint8_t*)arena_allocate_aligned(
        arena, total, kClosureAllocationAlignment);
    auto* closure = (eshkol_closure_t*)storage;
    if (!closure) {
        eshkol_error("Failed to allocate closure structure");
        return nullptr;
    }

    initialize_closure(closure, func_ptr, packed_info, sexpr_ptr,
                       return_type_info, name);

    if (actual_num_captures > 0) {
        closure->env = (eshkol_closure_env_t*)(storage + env_offset);
        initialize_capture_environment(closure->env, packed_info,
                                       actual_num_captures);
    } else {
        closure->env = nullptr;
    }

    eshkol_debug("Allocated closure at %p with func_ptr=%p, env=%p (%zu captures), return_type=%d, arity=%d, name=%s",
                 (void*)closure, (void*)func_ptr, (void*)closure->env, actual_num_captures,
                 closure->return_type, closure->input_arity, name ? name : "(anonymous)");

    return closure;
}

/**
 * @brief Allocate a closure object with a heap object header (consolidated form).
 *
 * Like arena_allocate_closure, but prepends an eshkol_object_header_t so the
 * closure can be referenced as a tagged HEAP_PTR/CALLABLE value and
 * dispatched on by subtype. The header subtype is
 * CALLABLE_SUBTYPE_LAMBDA_SEXPR when the closure has zero captures, or
 * CALLABLE_SUBTYPE_CLOSURE otherwise. As in arena_allocate_closure, a
 * non-zero capture count reserves the closure environment in the same
 * allocation and stores the packed capture/arity/variadic info into it.
 * Everything returned is arena-owned and lives until the arena is freed/reset; unlike
 * arena_allocate_closure, this variant does not log on success (only on
 * allocation failure).
 *
 * @param arena             Arena to allocate from (must not be null).
 * @param func_ptr          Pointer to the generated lambda function.
 * @param packed_info       Packed capture count / fixed-param count / variadic bit.
 * @param sexpr_ptr         Pointer to the closure's S-expression form, or 0.
 * @param return_type_info  Packed return-type byte, arity byte, and HoTT type id.
 * @param name              Bound name for a named closure, or nullptr for anonymous.
 * @return                  Newly allocated closure (with header), or nullptr on failure.
 */
eshkol_closure_t* arena_allocate_closure_with_header(arena_t* arena, uint64_t func_ptr,
                                                     uint64_t packed_info, uint64_t sexpr_ptr,
                                                     uint64_t return_type_info,
                                                     const char* name) {
    if (!arena) {
        eshkol_error("Cannot allocate closure with header: null arena");
        return nullptr;
    }

    const size_t actual_num_captures = CLOSURE_ENV_GET_NUM_CAPTURES(packed_info);

    const size_t data_size = sizeof(eshkol_closure_t);
    if (!eshkol_object_payload_fits(data_size)) {
        eshkol_error("Closure payload exceeds uint32_t header limit");
        return nullptr;
    }
    size_t closure_end = 0;
    size_t total = 0;
    if (!checked_add(sizeof(eshkol_object_header_t), data_size, &closure_end)) {
        eshkol_error("Closure allocation size overflow");
        return nullptr;
    }
    total = closure_end;
    size_t env_offset = 0;
    if (actual_num_captures > 0) {
        size_t env_size = 0;
        if (!checked_align_up(closure_end, kClosureAllocationAlignment,
                              &env_offset) ||
            !checked_capture_storage_size(actual_num_captures, &env_size) ||
            !checked_add(env_offset, env_size, &total)) {
            eshkol_error("Closure allocation size overflow");
            return nullptr;
        }
    }

    // Header, closure, and capture environment share one arena allocation.
    // The payload still begins after exactly one object header; only private
    // tail padding is used to align the environment.
    auto* mem = (uint8_t*)arena_allocate_aligned(
        arena, total, kClosureAllocationAlignment);
    if (!mem) {
        eshkol_error("Failed to allocate closure with header");
        return nullptr;
    }

    auto* hdr = (eshkol_object_header_t*)mem;
    hdr->subtype = (actual_num_captures == 0) ?
        CALLABLE_SUBTYPE_LAMBDA_SEXPR : CALLABLE_SUBTYPE_CLOSURE;
    hdr->flags = 0;
    hdr->ref_count = 0;
    hdr->size = (uint32_t)data_size;

    auto* closure = (eshkol_closure_t*)(mem + sizeof(eshkol_object_header_t));
    initialize_closure(closure, func_ptr, packed_info, sexpr_ptr,
                       return_type_info, name);

    if (actual_num_captures > 0) {
        closure->env = (eshkol_closure_env_t*)(mem + env_offset);
        initialize_capture_environment(closure->env, packed_info,
                                       actual_num_captures);
    } else {
        closure->env = nullptr;
    }

    return closure;
}

}  // extern "C"
