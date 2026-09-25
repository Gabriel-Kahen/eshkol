# Focused checked-promotion acceptance suite. Include after the runtime targets
# and their public system dependencies. The parent owns the default-OFF option.
include_guard(GLOBAL)

if(NOT ESHKOL_PROMOTION_TESTING)
    return()
endif()
if(NOT ESHKOL_BUILD_TESTS)
    message(FATAL_ERROR
        "ESHKOL_PROMOTION_TESTING requires ESHKOL_BUILD_TESTS=ON; "
        "instrumenting a runtime without its acceptance tests is not this mode.")
endif()
foreach(_promotion_runtime_target eshkol-runtime eshkol-runtime-core-obj eshkol-runtime-hosted-obj)
    if(NOT TARGET ${_promotion_runtime_target})
        message(FATAL_ERROR "CheckedPromotionTests must be included after ${_promotion_runtime_target}")
    endif()
endforeach()

# Transaction/unwind/identity tests do not need linker interception. The full
# acceptance mode also requires two --wrap fixtures and Itanium new/new[] names;
# do not silently report a portable subset as successful full-suite coverage.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
   NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    message(FATAL_ERROR
        "The complete ESHKOL_PROMOTION_TESTING suite is not available on this "
        "platform: external-caller and no-allocation probes require 64-bit Linux with "
        "GNU/Clang and a --wrap-capable linker. Disable this explicit test mode "
        "for ordinary builds; no substitute allocation proof is provided.")
endif()

target_compile_definitions(eshkol-runtime-core-obj PRIVATE ESHKOL_PROMOTION_TESTING=1)
target_compile_definitions(eshkol-runtime-hosted-obj PRIVATE ESHKOL_PROMOTION_TESTING=1)
find_package(Threads REQUIRED)

function(eshkol_add_checked_promotion_test target)
    add_executable(${target} ${ARGN})
    target_compile_features(${target} PRIVATE cxx_std_20)
    eshkol_apply_common_compile_settings(${target})
    target_compile_definitions(${target} PRIVATE ESHKOL_PROMOTION_TESTING=1)
    target_link_libraries(${target} PRIVATE
        eshkol-runtime ${ESHKOL_EXTRA_LINK_LIBS} Threads::Threads ${CMAKE_DL_LIBS} m)
    # Directory sanitizer compile/link options from CMakeLists apply normally;
    # no fixture bypasses them with a separate compiler invocation.
    add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
    set_tests_properties(${target} PROPERTIES
        LABELS "checked-promotion;native"
        ENVIRONMENT "ESHKOL_ARENA_POISON=1"
        TIMEOUT 60)
endfunction()

eshkol_add_checked_promotion_test(runtime_promotion_transaction_test
    tests/core/runtime_promotion_transaction_test.cpp)
eshkol_add_checked_promotion_test(runtime_promotion_unwind_test
    tests/core/runtime_promotion_unwind_test.cpp)
eshkol_add_checked_promotion_test(runtime_emergency_semantics_test
    tests/core/runtime_emergency_semantics_test.cpp)
eshkol_add_checked_promotion_test(checked_promotion_external_callers_test
    tests/core/checked_promotion_external_callers_test.cpp)
target_link_options(checked_promotion_external_callers_test PRIVATE
    -Wl,--wrap=malloc -Wl,--wrap=realloc -Wl,--wrap=free)
eshkol_add_checked_promotion_test(runtime_promotion_noalloc_transfer_test
    tests/core/runtime_promotion_noalloc_transfer_test.cpp
    tests/core/runtime_promotion_allocation_probe.cpp)
foreach(_promotion_wrapped_symbol malloc calloc realloc aligned_alloc posix_memalign
                                  _Znwm _Znam __cxa_allocate_exception)
    target_link_options(runtime_promotion_noalloc_transfer_test PRIVATE
        "-Wl,--wrap=${_promotion_wrapped_symbol}")
endforeach()

eshkol_add_checked_promotion_test(runtime_promotion_layout_lifetime_test
    tests/core/runtime_promotion_layout_lifetime_test.cpp)
eshkol_add_checked_promotion_test(runtime_root_arena_failure_test
    tests/core/runtime_root_arena_failure_test.cpp)
target_link_options(runtime_root_arena_failure_test PRIVATE
    -Wl,--wrap=arena_create_threadsafe)
eshkol_add_checked_promotion_test(runtime_exception_handler_reserve_test
    tests/core/runtime_exception_handler_reserve_test.cpp)
target_link_options(runtime_exception_handler_reserve_test PRIVATE
    -Wl,--wrap=malloc)

# Generated objects use the just-built compiler. Native shims/runtime and final
# linkage inherit the ordinary platform and sanitizer settings; no fixed host
# compiler path or ad-hoc system-library list is used.
function(eshkol_add_promotion_aot target source shim)
    set(optimization_level 2)
    if(ARGC GREATER 3)
        set(optimization_level "${ARGV3}")
    endif()
    set(object "${CMAKE_CURRENT_BINARY_DIR}/${target}.o")
    add_custom_command(OUTPUT "${object}"
        BYPRODUCTS "${object}.ll" "${object}.bc"
        COMMAND $<TARGET_FILE:eshkol-run> --no-stdlib -O ${optimization_level}
            --dump-ir --compile-only
            -o "${object}" "${CMAKE_CURRENT_SOURCE_DIR}/${source}"
        DEPENDS eshkol-run "${source}"
        VERBATIM)
    set_source_files_properties("${object}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
    eshkol_add_checked_promotion_test(${target} "${shim}" "${object}")
endfunction()
eshkol_add_promotion_aot(constructor_emergency_aot
    tests/core/constructor_emergency_test.esk tests/core/constructor_emergency_shim.cpp)
eshkol_add_promotion_aot(constructor_emergency_aot_o0
    tests/core/constructor_emergency_test.esk
    tests/core/constructor_emergency_shim.cpp 0)
foreach(_constructor_target constructor_emergency_aot constructor_emergency_aot_o0)
    target_link_options(${_constructor_target} PRIVATE
        -Wl,--wrap=arena_allocate
        -Wl,--wrap=arena_allocate_ad_node_with_header
        -Wl,--wrap=arena_allocate_closure_with_header
        -Wl,--wrap=arena_allocate_vector_with_header
        -Wl,--wrap=arena_allocate_cons_with_header
        -Wl,--wrap=arena_allocate_string_with_header
        -Wl,--wrap=arena_allocate_tensor_with_header
        -Wl,--wrap=arena_allocate_with_header
        -Wl,--wrap=eshkol_runtime_emergency_raise_v1
        -Wl,--wrap=malloc)
endforeach()

add_executable(constructor_emergency_jit
    tests/core/constructor_emergency_jit_test.cpp)
target_compile_features(constructor_emergency_jit PRIVATE cxx_std_17)
eshkol_apply_common_compile_settings(constructor_emergency_jit)
target_compile_definitions(constructor_emergency_jit PRIVATE
    ESHKOL_LLVM_BACKEND_ENABLED=1)
target_include_directories(constructor_emergency_jit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc)
if(LLVM_LIBRARY_DIRS)
    target_link_directories(constructor_emergency_jit PRIVATE ${LLVM_LIBRARY_DIRS})
endif()
target_link_libraries(constructor_emergency_jit PRIVATE
    eshkol-repl-lib eshkol-static ${ESHKOL_EXTRA_LINK_LIBS}
    ${LLVM_LIBS_LIST} ${LLVM_SYSTEM_LIBS_LIST})
target_link_options(constructor_emergency_jit PRIVATE
    -Wl,--whole-archive
    "$<TARGET_FILE:eshkol-static>"
    "$<TARGET_FILE:eshkol-repl-lib>"
    -Wl,--no-whole-archive
    -Wl,--export-dynamic
    -Wl,--unresolved-symbols=ignore-in-object-files
    -Wl,--wrap=arena_allocate_vector_with_header
    -Wl,--wrap=eshkol_runtime_emergency_raise_v1)
add_test(NAME constructor_emergency_jit_o0 COMMAND constructor_emergency_jit 0)
add_test(NAME constructor_emergency_jit_o2 COMMAND constructor_emergency_jit 2)
set_tests_properties(constructor_emergency_jit_o0 constructor_emergency_jit_o2 PROPERTIES
    LABELS "checked-promotion;jit" ENVIRONMENT "ESHKOL_JIT_CACHE=0" TIMEOUT 60)
eshkol_add_promotion_aot(checked_barrier_aot
    tests/core/checked_barrier_aot_test.esk tests/core/checked_barrier_aot_shim.cpp)
eshkol_add_promotion_aot(exception_handler_reserve_aot
    tests/core/exception_handler_reserve_symbol_test.esk
    tests/core/exception_handler_reserve_aot_shim.cpp)
eshkol_add_promotion_aot(runtime_emergency_rethrow_modifier_aot
    tests/core/runtime_emergency_rethrow_modifier_test.esk
    tests/core/runtime_emergency_rethrow_modifier_aot_shim.cpp)
add_test(NAME exception_handler_reserve_jit
    COMMAND $<TARGET_FILE:eshkol-run> --no-stdlib -O 2 --run
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/core/exception_handler_reserve_symbol_test.esk")
set_tests_properties(exception_handler_reserve_jit PROPERTIES
    LABELS "checked-promotion;jit" TIMEOUT 60)
add_test(NAME runtime_emergency_rethrow_modifier_jit
    COMMAND $<TARGET_FILE:eshkol-run> --no-stdlib -O 2 --run
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/core/runtime_emergency_rethrow_modifier_test.esk")
set_tests_properties(runtime_emergency_rethrow_modifier_jit PROPERTIES
    LABELS "checked-promotion;jit"
    ENVIRONMENT "ESHKOL_JIT_CACHE=0"
    TIMEOUT 60)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
execute_process(
    COMMAND "${LLVM_CONFIG_EXECUTABLE}" --bindir
    RESULT_VARIABLE _promotion_llvm_bindir_result
    OUTPUT_VARIABLE _promotion_llvm_bin_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _promotion_llvm_bindir_result EQUAL 0 OR
   NOT IS_DIRECTORY "${_promotion_llvm_bin_dir}")
    message(FATAL_ERROR
        "checked promotion bitcode verification could not query the LLVM bin "
        "directory from ${LLVM_CONFIG_EXECUTABLE}")
endif()
find_program(_promotion_llvm_dis
    NAMES llvm-dis "llvm-dis-${ESHKOL_REQUIRED_LLVM_MAJOR}"
    HINTS "${_promotion_llvm_bin_dir}" NO_DEFAULT_PATH)
if(NOT _promotion_llvm_dis)
    message(FATAL_ERROR
        "checked promotion bitcode verification requires llvm-dis from "
        "${_promotion_llvm_bin_dir}")
endif()
add_test(NAME checked_promotion_ir_dominance
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/core/check_checked_barrier_ir.py"
        "${CMAKE_CURRENT_BINARY_DIR}/checked_barrier_aot.o.ll")
add_test(NAME checked_constructor_ir_dominance
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/core/check_constructor_emergency_ir.py"
        "${CMAKE_CURRENT_BINARY_DIR}/constructor_emergency_aot.o.ll")
add_test(NAME checked_constructor_optimized_ir_dominance
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/core/check_constructor_emergency_ir.py"
        "${CMAKE_CURRENT_BINARY_DIR}/constructor_emergency_aot.o.bc"
        "${_promotion_llvm_dis}")
add_test(NAME checked_constructor_ir_dominance_o0
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/core/check_constructor_emergency_ir.py"
        "${CMAKE_CURRENT_BINARY_DIR}/constructor_emergency_aot_o0.o.ll")
add_test(NAME checked_constructor_optimized_ir_dominance_o0
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/core/check_constructor_emergency_ir.py"
        "${CMAKE_CURRENT_BINARY_DIR}/constructor_emergency_aot_o0.o.bc"
        "${_promotion_llvm_dis}")
set_tests_properties(checked_constructor_ir_dominance
    checked_constructor_ir_dominance_o0 PROPERTIES
    LABELS "checked-promotion;ir" TIMEOUT 60)
set_tests_properties(checked_constructor_optimized_ir_dominance
    checked_constructor_optimized_ir_dominance_o0 PROPERTIES
    LABELS "checked-promotion;ir" TIMEOUT 60)
set_tests_properties(checked_promotion_ir_dominance PROPERTIES
    LABELS "checked-promotion;ir" TIMEOUT 60)

add_custom_target(checked-promotion-tests DEPENDS
    runtime_root_arena_failure_test
    runtime_exception_handler_reserve_test
    runtime_promotion_layout_lifetime_test constructor_emergency_aot
    constructor_emergency_aot_o0
    constructor_emergency_jit checked_barrier_aot
    exception_handler_reserve_aot
    runtime_emergency_rethrow_modifier_aot
    runtime_promotion_transaction_test
    runtime_promotion_unwind_test
    runtime_emergency_semantics_test
    checked_promotion_external_callers_test
    runtime_promotion_noalloc_transfer_test)
