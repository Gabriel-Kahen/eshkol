#include "../../lib/core/arena_memory.h"

#include <eshkol/eshkol.h>
#include <eshkol/core/introspection.h>
#include <eshkol/llvm_backend.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>

extern "C" void* __real_arena_allocate_vector_with_header(arena_t*, size_t);
extern "C" [[noreturn]] void __real_eshkol_runtime_emergency_raise_v1(int32_t);
extern "C" void eshkol_get_raised_value(eshkol_tagged_value_t*);

namespace {

bool armed = true;
int wrapper_hits = 0;
int emergency_transfers = 0;
int condition_checks = 0;

int fail(const char* message) {
    std::fprintf(stderr, "FAIL JIT arena guard: %s (armed=%d hits=%d)\n",
                 message, armed ? 1 : 0, wrapper_hits);
    return 1;
}

}  // namespace

bool caught_canonical_allocation_condition() {
    eshkol_tagged_value_t value{};
    eshkol_get_raised_value(&value);
    if (value.type != ESHKOL_VALUE_HEAP_PTR || value.flags || value.reserved) {
        return false;
    }
    auto* exception = reinterpret_cast<eshkol_exception_t*>(value.data.ptr_val);
    if (!exception || g_current_exception != nullptr ||
        std::strcmp(exception->message,
                    "object or exception-handler allocation failed") != 0) {
        return false;
    }
    ++condition_checks;
    return true;
}

extern "C" [[noreturn]] void __wrap_eshkol_runtime_emergency_raise_v1(
    int32_t condition) {
    if (condition != 5) std::abort();
    ++emergency_transfers;
    __real_eshkol_runtime_emergency_raise_v1(condition);
}

extern "C" void* __wrap_arena_allocate_vector_with_header(arena_t* arena,
                                                            size_t capacity) {
    if (!armed) {
        return __real_arena_allocate_vector_with_header(arena, capacity);
    }
    armed = false;
    ++wrapper_hits;
    if (!arena || !arena->current_block) return nullptr;
    arena_block_t* const block = arena->current_block;
    const size_t saved_used = block->used;
    const bool saved_bounded = arena->bounded;
    arena->bounded = true;
    block->used = block->size;
    void* result = __real_arena_allocate_vector_with_header(arena, capacity);
    const bool genuine_null = result == nullptr &&
                              arena->current_block == block &&
                              block->used == block->size;
    block->used = saved_used;
    arena->bounded = saved_bounded;
    if (!genuine_null) std::abort();
    return result;
}

int main(int argc, char** argv) {
    const int optimization_level = argc > 1 ? std::atoi(argv[1]) : 0;
    if (optimization_level != 0 && optimization_level != 2) {
        return fail("expected optimization level 0 or 2");
    }
    eshkol_set_optimization_level(optimization_level);
    eshkol_tagged_value_t caught = eshkol_eval_string(
        "(guard (condition (else 77)) (vector 1 2))", nullptr);
    if (armed || wrapper_hits != 1 || emergency_transfers != 1 ||
        !caught_canonical_allocation_condition() || condition_checks != 1) {
        return fail("JIT did not resolve the wrapped allocator exactly once");
    }
    if (caught.type != ESHKOL_VALUE_INT64 || caught.data.int_val != 77) {
        return fail("JIT allocation failure did not reach the guard as condition 5");
    }

    eshkol_tagged_value_t retry = eshkol_eval_string(
        "(vector-length (vector 1 2 3))", nullptr);
    if (retry.type != ESHKOL_VALUE_INT64 || retry.data.int_val != 3) {
        return fail("JIT retry after caught bounded failure did not succeed");
    }
    if (wrapper_hits != 1 || emergency_transfers != 1 || condition_checks != 1) {
        return fail("retry unexpectedly reinjected failure or emergency transfer");
    }

    std::puts("PASS JIT genuine bounded null, condition 5, and retry");
    return 0;
}
