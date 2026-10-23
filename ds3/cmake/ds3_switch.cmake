# ds3_switch.cmake - what the codegen-emitted rexglue.cmake gives a title, for the Switch build.
#
#   DS3_CODEGEN_DIR   tools/codegen.sh's output (holds generated/default/, made from your deadspace_game_f.xex + .xexp)
#   REXSDK_DIR          this repository's sdk/ (rexglue-sdk-nx)
#
# The generated tree is used as it is, with one exception: its PCH. Its files include "ds3_pch.h" with
# quotes, which finds the copy next to them first, so the build compiles a mirror of symlinks (tools/prepare_tree.py)
# whose PCH is the Switch one (tools/make_nx_pch.py).

set(DS3_CODEGEN_DIR "" CACHE PATH "tools/codegen.sh output folder (holds generated/default/)")
set(DS3_GENERATED "${DS3_CODEGEN_DIR}/generated/default")
if(NOT EXISTS "${DS3_GENERATED}/sources.cmake")
    message(FATAL_ERROR
        "DS3_CODEGEN_DIR must point at tools/codegen.sh's output "
        "(no generated/default/sources.cmake under '${DS3_CODEGEN_DIR}')")
endif()

set(REXSDK_DIR "" CACHE PATH "Path to the rexglue-sdk(-nx) source tree")
if(NOT REXSDK_DIR)
    get_filename_component(REXSDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../sdk" ABSOLUTE)
endif()
add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
message(STATUS "ds3: ReXGlue SDK from ${REXSDK_DIR}")

# The mirror of the generated tree with the Switch PCH.
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
set(DS3_TREE "${CMAKE_BINARY_DIR}/generated_nx")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../tools/prepare_tree.py"
            "${DS3_GENERATED}" "${DS3_TREE}"
    RESULT_VARIABLE _ds3_tree_rc)
if(NOT _ds3_tree_rc EQUAL 0)
    message(FATAL_ERROR "ds3: tools/prepare_tree.py failed (${_ds3_tree_rc})")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${DS3_GENERATED}/ds3_pch.h"
    "${DS3_GENERATED}/sources.cmake")

set(REXGLUE_RECOMP_DEBUG_INFO "none" CACHE STRING
    "Debug info level for generated code: line-tables-only, full, or none")
set(REXGLUE_RECOMP_OPTIONS "")
if(REXGLUE_RECOMP_DEBUG_INFO STREQUAL "none")
    list(APPEND REXGLUE_RECOMP_OPTIONS $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-g0>)
elseif(REXGLUE_RECOMP_DEBUG_INFO STREQUAL "line-tables-only")
    list(APPEND REXGLUE_RECOMP_OPTIONS $<$<CXX_COMPILER_ID:Clang,AppleClang>:-gline-tables-only>)
endif()

# generated/default/sources.cmake: the support units and the recompiled ones. CMAKE_CURRENT_LIST_DIR there is the mirror.
include("${DS3_TREE}/sources.cmake")
set(DS3_GUEST_SOURCES ${GENERATED_SOURCES})
set_source_files_properties(${DS3_GUEST_SOURCES} PROPERTIES COMPILE_OPTIONS "${REXGLUE_RECOMP_OPTIONS}")
# The registration unit is one function of ~55,000 SetFunction calls, run once at startup: at -O3 GCC spends ~6 minutes
# on it alone, on one core, longer than all the other units together on ten. -O1 takes seconds. It needs nothing from
# the PCH (its own includes), and a PCH built at -O3 cannot be used at -O1.
set_source_files_properties("${DS3_TREE}/ds3_register.cpp" PROPERTIES
    COMPILE_OPTIONS "${REXGLUE_RECOMP_OPTIONS};-O1"
    SKIP_PRECOMPILE_HEADERS ON)
list(LENGTH DS3_GUEST_SOURCES _ds3_n)
message(STATUS "ds3: ${_ds3_n} recompiled sources from ${DS3_GENERATED}")

# Usage: ds3_setup_target(<target>)
macro(ds3_setup_target target_name)
    add_library(${target_name}_recomp OBJECT ${DS3_GUEST_SOURCES})
    target_include_directories(${target_name}_recomp PRIVATE "${DS3_TREE}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(${target_name}_recomp PRIVATE rex::runtime)
    rexglue_apply_target_settings(${target_name}_recomp)
    target_precompile_headers(${target_name}_recomp PRIVATE "${DS3_TREE}/ds3_pch.h")
    target_compile_options(${target_name}_recomp PRIVATE ${REXGLUE_RECOMP_OPTIONS})
    target_link_libraries(${target_name} PRIVATE ${target_name}_recomp)
    target_include_directories(${target_name} PRIVATE "${DS3_TREE}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(${target_name} PRIVATE rex::runtime)
    rexglue_configure_target(${target_name} ${ARGN})
endmacro()
