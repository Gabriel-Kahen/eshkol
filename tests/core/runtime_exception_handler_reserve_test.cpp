// Checked exception-handler reservation: exact pool capacity, allocation
// failure transfer, reuse, and sequential thread-local isolation.
#include "../../lib/core/runtime_region_promotion_internal.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <setjmp.h>
#include <thread>

extern "C" void* __real_malloc(size_t);
extern "C" void eshkol_get_raised_value(eshkol_tagged_value_t*);

namespace {
std::atomic<bool> track_handler_malloc{false};
std::atomic<int64_t> successful_handler_mallocs_before_failure{-1};
std::atomic<uint64_t> handler_malloc_calls{0};
eshkol_tagged_value_t rethrown_value{};

void check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL handler reserve: %s\n", message);
        std::abort();
    }
}

void arm_malloc(int64_t successful_calls) {
    handler_malloc_calls.store(0, std::memory_order_relaxed);
    successful_handler_mallocs_before_failure.store(
        successful_calls, std::memory_order_relaxed);
    track_handler_malloc.store(true, std::memory_order_release);
}

void disarm_malloc() {
    track_handler_malloc.store(false, std::memory_order_release);
    successful_handler_mallocs_before_failure.store(-1,
                                                     std::memory_order_relaxed);
}

bool current_condition(const char* message) {
    return g_current_exception && g_current_exception->message &&
           std::strcmp(g_current_exception->message, message) == 0;
}

void release_pool() {
    check(g_exception_handler_stack == nullptr, "pool release requires no active handler");
    eshkol_promotion_test_exception_handler_pool_release();
    check(eshkol_promotion_test_exception_handler_pool_size() == 0,
          "test pool release drains the calling thread");
}

void expect_invalid_without_pool_mutation(int64_t requested) {
    check(eshkol_promotion_test_exception_handler_pool_size() == 0,
          "invalid-count fixture starts with an empty pool");
    jmp_buf outer;
    const int transferred = setjmp(outer);
    if (transferred == 0) {
        eshkol_push_exception_handler(&outer);
        check(eshkol_promotion_test_exception_handler_pool_size() == 0,
              "active outer frame is not inactive capacity");
        eshkol_promotion_test_emergency_reset();
        arm_malloc(0);
        (void)eshkol_runtime_reserve_exception_handlers_v1(requested);
        disarm_malloc();
        check(false, "invalid reserve count must transfer");
    }
    disarm_malloc();
    check(handler_malloc_calls.load(std::memory_order_relaxed) == 0,
          "invalid reserve count performs no allocation");
    check(eshkol_promotion_test_exception_handler_pool_size() == 0,
          "invalid reserve count does not mutate the inactive pool");
    check(current_condition("region promotion runtime state is invalid"),
          "invalid reserve count transfers canonical condition 4");
    check(eshkol_promotion_test_emergency_transfers() == 1,
          "invalid reserve count transfers exactly once");
    eshkol_pop_exception_handler();
    check(eshkol_promotion_test_exception_handler_pool_size() == 1,
          "outer frame becomes reusable after invalid-count catch");
    release_pool();
}

void push_without_allocation(unsigned depth) {
    if (depth == 0) return;
    jmp_buf handler;
    check(setjmp(handler) == 0, "reserved push unexpectedly received a transfer");
    eshkol_push_exception_handler(&handler);
    push_without_allocation(depth - 1);
    eshkol_pop_exception_handler();
}

void test_zero_invalid_and_overflow() {
    release_pool();
    arm_malloc(0);
    check(eshkol_runtime_reserve_exception_handlers_v1(0) == 0,
          "zero reservation succeeds");
    disarm_malloc();
    check(handler_malloc_calls.load(std::memory_order_relaxed) == 0,
          "zero reservation performs no allocation");
    check(eshkol_promotion_test_exception_handler_pool_size() == 0,
          "zero reservation leaves an empty pool unchanged");

    expect_invalid_without_pool_mutation(-1);
    expect_invalid_without_pool_mutation(std::numeric_limits<int64_t>::max());
}

void test_partial_failure_and_retry() {
    release_pool();
    jmp_buf outer;
    const int transferred = setjmp(outer);
    if (transferred == 0) {
        eshkol_push_exception_handler(&outer);
        eshkol_promotion_test_emergency_reset();
        arm_malloc(2);
        (void)eshkol_runtime_reserve_exception_handlers_v1(5);
        disarm_malloc();
        check(false, "third reservation allocation must fail");
    }
    disarm_malloc();
    check(handler_malloc_calls.load(std::memory_order_relaxed) == 3,
          "partial reservation reaches the configured failing allocation");
    check(eshkol_promotion_test_exception_handler_pool_size() == 2,
          "successful reservation prefix remains reusable");
    check(current_condition("object or exception-handler allocation failed"),
          "reservation malloc failure transfers canonical condition 5");
    check(eshkol_promotion_test_emergency_transfers() == 1,
          "reservation malloc failure transfers exactly once");
    eshkol_pop_exception_handler();
    check(eshkol_promotion_test_exception_handler_pool_size() == 3,
          "established catcher is recycled beside the partial prefix");

    arm_malloc(-1);
    check(eshkol_runtime_reserve_exception_handlers_v1(5) == 0,
          "reservation retry succeeds");
    disarm_malloc();
    check(handler_malloc_calls.load(std::memory_order_relaxed) == 2,
          "retry allocates only the missing capacity");
    check(eshkol_promotion_test_exception_handler_pool_size() == 5,
          "retry reaches the exact requested capacity");

    arm_malloc(0);
    check(eshkol_runtime_reserve_exception_handlers_v1(5) == 0,
          "satisfied reservation is idempotent");
    disarm_malloc();
    check(handler_malloc_calls.load(std::memory_order_relaxed) == 0,
          "idempotent reservation performs no allocation");
    release_pool();
}

void test_simultaneous_push_guarantee_and_reuse() {
    release_pool();
    arm_malloc(-1);
    check(eshkol_runtime_reserve_exception_handlers_v1(5) == 0,
          "five-frame reservation succeeds");
    disarm_malloc();
    check(handler_malloc_calls.load(std::memory_order_relaxed) == 5,
          "empty pool reservation allocates exactly five frames");

    for (int repetition = 0; repetition < 3; ++repetition) {
        arm_malloc(0);
        push_without_allocation(5);
        disarm_malloc();
        check(handler_malloc_calls.load(std::memory_order_relaxed) == 0,
              "five simultaneous pushes consume no malloc after reserve");
        check(eshkol_promotion_test_exception_handler_pool_size() == 5,
              "five popped frames replenish the reservation");
    }
    release_pool();
}

void test_reserved_plus_one_and_exact_rethrow() {
    release_pool();
    check(eshkol_runtime_reserve_exception_handlers_v1(2) == 0,
          "two-frame reservation succeeds");
    eshkol_promotion_test_emergency_reset();
    rethrown_value = {};
    arm_malloc(0);

    jmp_buf outer;
    if (setjmp(outer) != 0) {
        disarm_malloc();
        check(handler_malloc_calls.load(std::memory_order_relaxed) == 1,
              "reserved-plus-one attempts one malloc");
        check(current_condition("object or exception-handler allocation failed"),
              "reserved-plus-one and rethrow preserve condition 5");
        check(rethrown_value.type == ESHKOL_VALUE_HEAP_PTR &&
                  rethrown_value.flags == 0 && rethrown_value.reserved == 0 &&
                  rethrown_value.data.ptr_val ==
                      reinterpret_cast<uintptr_t>(g_current_exception),
              "rethrow preserves the exact canonical tagged identity");
        check(eshkol_promotion_test_emergency_transfers() == 2,
              "allocation failure and exact rethrow transfer twice");
        eshkol_pop_exception_handler();
        check(g_exception_handler_stack == nullptr,
              "rethrow unwinds to and removes the outer catcher");
        check(eshkol_promotion_test_exception_handler_pool_size() == 2,
              "caught and rethrown handlers replenish both reserved frames");
    } else {
        eshkol_push_exception_handler(&outer);
        jmp_buf inner;
        if (setjmp(inner) != 0) {
            eshkol_get_raised_value(&rethrown_value);
            eshkol_pop_exception_handler();
            eshkol_runtime_emergency_rethrow_if_v1(&rethrown_value);
            disarm_malloc();
            check(false, "canonical condition 5 rethrow must transfer");
        }
        eshkol_push_exception_handler(&inner);
        jmp_buf rejected;
        check(setjmp(rejected) == 0,
              "unpublished reserved-plus-one buffer cannot receive transfer");
        eshkol_push_exception_handler(&rejected);
        disarm_malloc();
        check(false, "reserved-plus-one push must fail under persistent malloc refusal");
    }

    arm_malloc(0);
    push_without_allocation(2);
    disarm_malloc();
    check(handler_malloc_calls.load(std::memory_order_relaxed) == 0,
          "rethrow path leaves both frames reusable without malloc");
    release_pool();
}

void test_sequential_thread_local_isolation() {
    release_pool();
    check(eshkol_runtime_reserve_exception_handlers_v1(1) == 0,
          "main thread reserves one frame");
    check(eshkol_promotion_test_exception_handler_pool_size() == 1,
          "main thread observes its one-frame pool");

    std::thread worker([] {
        check(eshkol_promotion_test_exception_handler_pool_size() == 0,
              "new sequential worker does not inherit main-thread capacity");
        arm_malloc(-1);
        check(eshkol_runtime_reserve_exception_handlers_v1(2) == 0,
              "worker reserves two local frames");
        disarm_malloc();
        check(handler_malloc_calls.load(std::memory_order_relaxed) == 2,
              "worker reservation allocates its own two frames");
        check(eshkol_promotion_test_exception_handler_pool_size() == 2,
              "worker observes only its local capacity");
        arm_malloc(0);
        push_without_allocation(2);
        disarm_malloc();
        check(handler_malloc_calls.load(std::memory_order_relaxed) == 0,
              "worker consumes reserved local frames without malloc");
        eshkol_promotion_test_exception_handler_pool_release();
        check(eshkol_promotion_test_exception_handler_pool_size() == 0,
              "worker drains its pool before thread exit");
    });
    worker.join();

    check(eshkol_promotion_test_exception_handler_pool_size() == 1,
          "worker reserve and drain do not alter main-thread capacity");
    release_pool();
}
} // namespace

extern "C" void* __wrap_malloc(size_t bytes) {
    if (bytes == sizeof(eshkol_exception_handler_t) &&
        track_handler_malloc.load(std::memory_order_acquire)) {
        handler_malloc_calls.fetch_add(1, std::memory_order_relaxed);
        int64_t remaining = successful_handler_mallocs_before_failure.load(
            std::memory_order_relaxed);
        if (remaining == 0) return nullptr;
        if (remaining > 0) {
            successful_handler_mallocs_before_failure.fetch_sub(
                1, std::memory_order_relaxed);
        }
    }
    return __real_malloc(bytes);
}

int main() {
    test_zero_invalid_and_overflow();
    test_partial_failure_and_retry();
    test_simultaneous_push_guarantee_and_reuse();
    test_reserved_plus_one_and_exact_rethrow();
    test_sequential_thread_local_isolation();
    check(g_exception_handler_stack == nullptr, "final handler stack is empty");
    check(eshkol_promotion_test_exception_handler_pool_size() == 0,
          "final calling-thread pool is drained");
    std::puts("PASS checked exception-handler reservation, failure, reuse, and isolation");
}
