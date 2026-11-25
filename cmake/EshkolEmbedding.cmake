include(CMakePackageConfigHelpers)

if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin|Windows)$" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
   ESHKOL_XLA_ENABLED OR USING_STABLEHLO_LLVM OR GPU_FOUND OR
   ESHKOL_QUANTUM_ENABLED OR ESHKOL_QLLM_ENABLED OR ESHKOL_TENSORCORE_ENABLED OR
   ESHKOL_ENABLE_ASAN OR ESHKOL_ENABLE_UBSAN OR ESHKOL_ENABLE_TSAN OR
   ESHKOL_ENABLE_MSAN OR ESHKOL_ENABLE_LSAN)
    message(FATAL_ERROR "The embedding SDK requires a native CPU build without external tensor backends or sanitizers")
endif()
if(NOT TARGET eshkol-repl-lib)
    message(FATAL_ERROR "The embedding SDK requires the native REPL JIT library")
endif()

set(ESHKOL_EMBEDDING_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
if(WIN32 AND CMAKE_VS_PLATFORM_NAME)
    set(ESHKOL_EMBEDDING_ARCH "${CMAKE_VS_PLATFORM_NAME}")
elseif(WIN32 AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
    set(ESHKOL_EMBEDDING_ARCH "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
elseif(APPLE AND CMAKE_OSX_ARCHITECTURES)
    set(ESHKOL_EMBEDDING_ARCH "${CMAKE_OSX_ARCHITECTURES}")
endif()
string(TOLOWER "${ESHKOL_EMBEDDING_ARCH}" ESHKOL_EMBEDDING_ARCH)
if(ESHKOL_EMBEDDING_ARCH MATCHES "^(amd64|x64|x86_64)$")
    set(ESHKOL_EMBEDDING_ARCH x86_64)
elseif(ESHKOL_EMBEDDING_ARCH MATCHES "^(arm64|aarch64)$")
    set(ESHKOL_EMBEDDING_ARCH arm64)
else()
    message(FATAL_ERROR "The embedding SDK requires a single x86_64 or arm64 architecture")
endif()

set(ESHKOL_EMBEDDING_OPENSSL_COMPONENTS "")
if(ESHKOL_TLS_OPENSSL_FOUND OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET eshkol-agent-ffi))
    set(ESHKOL_EMBEDDING_OPENSSL_COMPONENTS SSL Crypto)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND OpenSSL_FOUND)
    set(ESHKOL_EMBEDDING_OPENSSL_COMPONENTS Crypto)
endif()
set(ESHKOL_EMBEDDING_BLAS "")
if(ESHKOL_BLAS_ENABLED AND BLAS_LIBRARIES)
    if(APPLE)
        set(ESHKOL_EMBEDDING_BLAS Accelerate)
    elseif(BLAS_LIBRARIES STREQUAL "PkgConfig::OPENBLAS")
        set(ESHKOL_EMBEDDING_BLAS OpenBLAS)
    else()
        set(ESHKOL_EMBEDDING_BLAS BLAS)
    endif()
endif()

set(_eshkol_embedding_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/EshkolEmbedding")
set(_eshkol_embedding_archive_dir "${CMAKE_INSTALL_LIBDIR}/eshkol")
set(_eshkol_embedding_configurations "")
set(_eshkol_embedding_export_file "")
set(ESHKOL_EMBEDDING_BUILTINS_NAME "")
if(WIN32)
    if(CMAKE_VERSION VERSION_LESS 3.20 OR NOT ESHKOL_USING_MSVC OR
       NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
       NOT ESHKOL_CLANG_BUILTINS_LIB OR NOT ESHKOL_RUNTIME_DEF_NM)
        message(FATAL_ERROR "The Windows embedding SDK requires CMake 3.20+, ClangCL with compiler-rt, and llvm-nm")
    endif()
    set(_eshkol_embedding_configurations CONFIGURATIONS Release)
    get_filename_component(ESHKOL_EMBEDDING_BUILTINS_NAME "${ESHKOL_CLANG_BUILTINS_LIB}" NAME)
    set(_eshkol_embedding_export_dir "${CMAKE_CURRENT_BINARY_DIR}/embedding-sdk/$<CONFIG>")
    set(_eshkol_embedding_export_file "${_eshkol_embedding_export_dir}/eshkol-embed-exports.def")
endif()

set(_eshkol_embedding_targets eshkol-static eshkol-repl-lib)
set(ESHKOL_EMBEDDING_AGENT_ARCHIVES "")
if(TARGET eshkol-agent-ffi)
    list(APPEND _eshkol_embedding_targets eshkol-agent-ffi)
    # Reuse the stable dependency filenames staged beside the production archive.
    set(_eshkol_embedding_agent_deps pcre2 sqlite3 zlib tree-sitter-grammars tree-sitter yoga)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND _eshkol_embedding_agent_deps curl)
    endif()
    foreach(_dep IN LISTS _eshkol_embedding_agent_deps)
        set(_dep_archive "eshkol-agent-${_dep}${CMAKE_STATIC_LIBRARY_SUFFIX}")
        list(APPEND ESHKOL_EMBEDDING_AGENT_ARCHIVES "${_dep_archive}")
        install(FILES "$<TARGET_FILE_DIR:eshkol-agent-ffi>/${_dep_archive}"
            ${_eshkol_embedding_configurations}
            DESTINATION "${_eshkol_embedding_archive_dir}" COMPONENT EmbeddingSDK)
    endforeach()
endif()
set(ESHKOL_EMBEDDING_MACOS_LIBRARIES "")
if(APPLE)
    set(ESHKOL_EMBEDDING_MACOS_LIBRARIES ${ESHKOL_EXTRA_LINK_LIBS}
        "-framework Foundation" "-framework CoreFoundation" "-framework Security")
    if(TARGET eshkol-agent-ffi)
        list(APPEND ESHKOL_EMBEDDING_MACOS_LIBRARIES "-framework SystemConfiguration")
    endif()
endif()

if(WIN32)
    set(_eshkol_embedding_export_archives "")
    foreach(_archive IN LISTS _eshkol_embedding_targets)
        list(APPEND _eshkol_embedding_export_archives "$<TARGET_FILE:${_archive}>")
    endforeach()
    list(APPEND _eshkol_embedding_export_archives "${ESHKOL_CLANG_BUILTINS_LIB}")
    add_custom_command(OUTPUT "${_eshkol_embedding_export_file}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_eshkol_embedding_export_dir}"
        COMMAND "${CMAKE_COMMAND}"
            "-DARCHIVES=${_eshkol_embedding_export_archives}"
            "-DNM=${ESHKOL_RUNTIME_DEF_NM}" "-DOUTPUT=${_eshkol_embedding_export_file}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen_runtime_def.cmake"
        DEPENDS ${_eshkol_embedding_targets} "${ESHKOL_CLANG_BUILTINS_LIB}"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen_runtime_def.cmake"
        VERBATIM)
    install(FILES "${_eshkol_embedding_export_file}"
        CONFIGURATIONS Release DESTINATION "${_eshkol_embedding_archive_dir}" COMPONENT EmbeddingSDK)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EshkolEmbeddingWindowsLLVM.cmake"
        CONFIGURATIONS Release DESTINATION "${_eshkol_embedding_cmake_dir}" COMPONENT EmbeddingSDK)
else()
    execute_process(COMMAND "${LLVM_CONFIG_EXECUTABLE}" --shared-mode
        OUTPUT_VARIABLE ESHKOL_EMBEDDING_LLVM_MODE OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _eshkol_embedding_llvm_result)
    if(NOT _eshkol_embedding_llvm_result EQUAL 0 OR NOT ESHKOL_EMBEDDING_LLVM_MODE MATCHES "^(shared|static)$")
        message(FATAL_ERROR "Could not determine the embedding SDK's LLVM linkage mode")
    endif()
endif()
add_custom_target(eshkol-embedding-sdk ALL
    DEPENDS ${_eshkol_embedding_targets} stdlib ${_eshkol_embedding_export_file})

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EshkolEmbeddingConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/EshkolEmbeddingConfig.cmake"
    INSTALL_DESTINATION "${_eshkol_embedding_cmake_dir}"
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/EshkolEmbeddingConfig.cmake"
    ${_eshkol_embedding_configurations}
    DESTINATION "${_eshkol_embedding_cmake_dir}" COMPONENT EmbeddingSDK)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/inc/eshkol/eshkol_ffi.h"
    ${_eshkol_embedding_configurations}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/eshkol" COMPONENT EmbeddingSDK)
install(TARGETS ${_eshkol_embedding_targets}
    ARCHIVE DESTINATION "${_eshkol_embedding_archive_dir}"
    COMPONENT EmbeddingSDK ${_eshkol_embedding_configurations})
# The SDK component can be staged separately from the runtime package.
install(FILES "${STDLIB_OUTPUT}" "${STDLIB_BITCODE}"
    ${_eshkol_embedding_configurations}
    DESTINATION "${_eshkol_embedding_archive_dir}" COMPONENT EmbeddingSDK)
