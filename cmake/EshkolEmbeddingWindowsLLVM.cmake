# Resolve stale DIA SDK paths in the Windows LLVM development package.
function(eshkol_relocate_windows_llvm_dia)
    if(TARGET LLVMDebugInfoPDB)
        get_target_property(_eshkol_pdb_link_libs LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
        if(_eshkol_pdb_link_libs)
            set(_eshkol_pdb_link_libs_patched "")
            foreach(_llvm_dep IN LISTS _eshkol_pdb_link_libs)
                if(_llvm_dep MATCHES "[/\\\\]DIA SDK[/\\\\]lib[/\\\\](amd64|arm64)[/\\\\]diaguids\\.lib$" AND
                   NOT EXISTS "${_llvm_dep}")
                    set(_eshkol_dia_arch "${CMAKE_MATCH_1}")
                    set(_eshkol_dia_sdk_libs "")

                    # LLVM's official Windows SDK has historically embedded
                    # the absolute DIA SDK path from the machine that produced
                    # LLVMExports.cmake.  Imported targets must remain relocatable:
                    # resolve the same architecture's GUID definitions from the
                    # active Visual Studio instance instead of retaining a stale
                    # VS edition/year path.
                    foreach(_eshkol_vs_root IN ITEMS
                            "${CMAKE_GENERATOR_INSTANCE}"
                            "$ENV{VSINSTALLDIR}")
                        if(NOT "${_eshkol_vs_root}" STREQUAL "")
                            file(TO_CMAKE_PATH "${_eshkol_vs_root}" _eshkol_vs_root_normalized)
                            list(APPEND _eshkol_dia_sdk_libs
                                "${_eshkol_vs_root_normalized}/DIA SDK/lib/${_eshkol_dia_arch}/diaguids.lib")
                        endif()
                    endforeach()
                    file(GLOB _eshkol_dia_sdk_globbed LIST_DIRECTORIES FALSE
                        "C:/Program Files/Microsoft Visual Studio/*/*/DIA SDK/lib/${_eshkol_dia_arch}/diaguids.lib"
                        "C:/Program Files (x86)/Microsoft Visual Studio/*/*/DIA SDK/lib/${_eshkol_dia_arch}/diaguids.lib"
                    )
                    list(APPEND _eshkol_dia_sdk_libs ${_eshkol_dia_sdk_globbed})

                    set(_eshkol_diaguids_replacement "")
                    foreach(_eshkol_dia_candidate IN LISTS _eshkol_dia_sdk_libs)
                        if(EXISTS "${_eshkol_dia_candidate}")
                            set(_eshkol_diaguids_replacement "${_eshkol_dia_candidate}")
                            break()
                        endif()
                    endforeach()

                    if(NOT _eshkol_diaguids_replacement)
                        message(FATAL_ERROR
                            "LLVM ${LLVM_PACKAGE_VERSION} references missing DIA library '${_llvm_dep}', "
                            "and no ${_eshkol_dia_arch} diaguids.lib was found in the active Visual Studio "
                            "instance '${CMAKE_GENERATOR_INSTANCE}'. Install the Visual Studio DIA SDK component.")
                    endif()

                    message(STATUS
                        "LLVM DIA SDK: replacing non-relocatable '${_llvm_dep}' with "
                        "'${_eshkol_diaguids_replacement}'")
                    list(APPEND _eshkol_pdb_link_libs_patched "${_eshkol_diaguids_replacement}")
                else()
                    list(APPEND _eshkol_pdb_link_libs_patched "${_llvm_dep}")
                endif()
            endforeach()
            set_target_properties(LLVMDebugInfoPDB PROPERTIES
                INTERFACE_LINK_LIBRARIES "${_eshkol_pdb_link_libs_patched}")
        endif()
    endif()
endfunction()
