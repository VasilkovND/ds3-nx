# ds3_switch.cmake - what the codegen-emitted rexglue.cmake and dll_targets.cmake give a title, for the Switch build.
#
#   DS3_CODEGEN_DIR   tools/codegen.sh's output: generated/default/ (default.xex, the title) and generated/game/
#                     (deadspace_game_f.xex, the game, a DLL the title loads), made from your executables + TU1
#   REXSDK_DIR        this repository's sdk/ (rexglue-sdk-nx)
#
# The generated trees are used as they are, with one exception: their PCH. Their files include "ds3_pch.h" with
# quotes, which finds the copy next to them first, so the build compiles mirrors of symlinks (tools/prepare_tree.py)
# whose PCH is the Switch one (tools/make_nx_pch.py).

set(DS3_CODEGEN_DIR "" CACHE PATH "tools/codegen.sh output folder (holds generated/default/ and generated/game/)")
foreach(_ds3_m default game)
    if(NOT EXISTS "${DS3_CODEGEN_DIR}/generated/${_ds3_m}/sources.cmake")
        message(FATAL_ERROR
            "DS3_CODEGEN_DIR must point at tools/codegen.sh's output "
            "(no generated/${_ds3_m}/sources.cmake under '${DS3_CODEGEN_DIR}')")
    endif()
endforeach()

set(REXSDK_DIR "" CACHE PATH "Path to the rexglue-sdk(-nx) source tree")
if(NOT REXSDK_DIR)
    get_filename_component(REXSDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../sdk" ABSOLUTE)
endif()
add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
message(STATUS "ds3: ReXGlue SDK from ${REXSDK_DIR}")

# The mirrors of the generated trees with the Switch PCH.
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
set(DS3_TREE "${CMAKE_BINARY_DIR}/generated_nx/default")
set(DS3_GAME_TREE "${CMAKE_BINARY_DIR}/generated_nx/game")
foreach(_ds3_m default game)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../tools/prepare_tree.py"
                "${DS3_CODEGEN_DIR}/generated/${_ds3_m}" "${CMAKE_BINARY_DIR}/generated_nx/${_ds3_m}"
        RESULT_VARIABLE _ds3_tree_rc)
    if(NOT _ds3_tree_rc EQUAL 0)
        message(FATAL_ERROR "ds3: tools/prepare_tree.py failed (${_ds3_tree_rc})")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${DS3_CODEGEN_DIR}/generated/${_ds3_m}/ds3_pch.h"
        "${DS3_CODEGEN_DIR}/generated/${_ds3_m}/sources.cmake")
endforeach()

set(REXGLUE_RECOMP_DEBUG_INFO "none" CACHE STRING
    "Debug info level for generated code: line-tables-only, full, or none")
set(REXGLUE_RECOMP_OPTIONS "")
if(REXGLUE_RECOMP_DEBUG_INFO STREQUAL "none")
    list(APPEND REXGLUE_RECOMP_OPTIONS $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-g0>)
elseif(REXGLUE_RECOMP_DEBUG_INFO STREQUAL "line-tables-only")
    list(APPEND REXGLUE_RECOMP_OPTIONS $<$<CXX_COMPILER_ID:Clang,AppleClang>:-gline-tables-only>)
endif()

# sources.cmake: the support units and the recompiled ones. CMAKE_CURRENT_LIST_DIR there is the mirror.
include("${DS3_TREE}/sources.cmake")
set(DS3_GUEST_SOURCES ${GENERATED_SOURCES})
include("${DS3_GAME_TREE}/sources.cmake")
set(DS3_GAME_SOURCES ${GENERATED_SOURCES})
unset(GENERATED_SOURCES)
set_source_files_properties(${DS3_GUEST_SOURCES} ${DS3_GAME_SOURCES} PROPERTIES COMPILE_OPTIONS "${REXGLUE_RECOMP_OPTIONS}")
# The game's registration unit is one function of ~55,000 SetFunction calls, run once at startup: at -O3 GCC spends
# ~6 minutes on it alone, on one core, longer than all the other units together on ten. -O1 takes seconds. It needs
# nothing from the PCH (its own includes), and a PCH built at -O3 cannot be used at -O1.
set_source_files_properties("${DS3_TREE}/ds3_register.cpp" "${DS3_GAME_TREE}/ds3_register.cpp" PROPERTIES
    COMPILE_OPTIONS "${REXGLUE_RECOMP_OPTIONS};-O1"
    SKIP_PRECOMPILE_HEADERS ON)
list(LENGTH DS3_GUEST_SOURCES _ds3_n)
list(LENGTH DS3_GAME_SOURCES _ds3_g)
message(STATUS "ds3: ${_ds3_n} recompiled sources for default.xex, ${_ds3_g} for deadspace_game_f.xex")

# Usage: ds3_setup_target(<target>): the title, with default.xex's code.
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

# Usage: ds3_setup_game_module(<title target> <module target> [hook sources...]): deadspace_game_f.xex's code as a
# guest module, linked into the NRO (rexglue_switch_link_module: its symbols made local, registered under the module's
# name, which generated/default/module_registry.cpp gives XexLoadImage). Hooks on the game's own functions must be in
# the module: they replace a function by defining its name, and the module's names are not visible outside it.
macro(ds3_setup_game_module host module_name)
    add_library(${module_name} STATIC ${DS3_GAME_SOURCES} ${ARGN})
    target_include_directories(${module_name} PRIVATE "${DS3_GAME_TREE}" "${CMAKE_CURRENT_SOURCE_DIR}/src"
                               "${CMAKE_CURRENT_SOURCE_DIR}/../shaders" "${REXSDK_DIR}/thirdparty/xxHash")
    target_link_libraries(${module_name} PRIVATE rex::runtime)
    target_precompile_headers(${module_name} PRIVATE "${DS3_GAME_TREE}/ds3_pch.h")
    target_compile_options(${module_name} PRIVATE ${REXGLUE_RECOMP_OPTIONS})
    set_target_properties(${module_name} PROPERTIES CXX_VISIBILITY_PRESET hidden)
    rexglue_configure_module_target(${module_name} HOST ${host})
endmacro()
