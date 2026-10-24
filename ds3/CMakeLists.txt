# ds3 - Dead Space 3 (Xbox 360, Title Update 1) on the Nintendo Switch.
#
# The recompiled code is generated from your own default.xex and deadspace_game_f.xex with their TU1 patches by
# tools/codegen.sh (ReXGlue's codegen, with codegen/names*.toml), never committed. Point DS3_CODEGEN_DIR at its output.
# At configure time tools/prepare_tree.py mirrors generated/default/ and generated/game/ as symlinks with a Switch PCH
# (tools/make_nx_pch.py); see PORTING.md. tools/build_mac.sh does all of it.
#
# Configure (Switch):
#   cmake -S ds3 -B ds3/out/sw -G Ninja -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=tools/switch/cmake/switch-devkitA64.cmake \
#     -DREXSDK_DIR=$PWD/sdk -DDS3_CODEGEN_DIR=$PWD/ds3/out/codegen -DREXGLUE_SWITCH_DEKO3D=ON
#   cmake --build ds3/out/sw

cmake_minimum_required(VERSION 3.25)
project(ds3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/ds3_switch.cmake)

if(NOT REXGLUE_SWITCH_DEKO3D)
    message(FATAL_ERROR "ds3: only the deko3d build exists (-DREXGLUE_SWITCH_DEKO3D=ON)")
endif()

add_executable(ds3
    src/main.cpp
    src/ds3_gpu.cpp
    src/ds3_shaders.cpp
    src/ds3_cpu.cpp
    src/ds3_disc_swap.cpp
    # The shader library reader, shared with the PC tools (shaders/).
    ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/ds_shader_library.cpp
)

ds3_setup_target(ds3)
rexglue_switch_add_deko3d(ds3)
target_compile_definitions(ds3 PRIVATE DS3_DEKO3D=1)
target_include_directories(ds3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shaders ${REXSDK_DIR}/thirdparty/xxHash)

# The game itself, deadspace_game_f.xex, a DLL default.xex loads: a guest module linked into the NRO. Its name is the
# one generated/default/module_registry.cpp registers it under.
# Hooks on game functions are compiled only when codegen/names.toml names them (the names exist in the generated code
# only then), and into the module (cmake/ds3_switch.cmake says why).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/codegen/names.toml" DS3_NAMES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/codegen/names.toml")
set(DS3_GAME_HOOKS "")
function(ds3_add_hook source)
    foreach(name IN LISTS ARGN)
        if(NOT DS3_NAMES MATCHES "\nname = \"${name}\"")
            message(STATUS "ds3: ${source} off (codegen/names.toml has no ${name})")
            return()
        endif()
    endforeach()
    list(APPEND DS3_GAME_HOOKS ${source})
    set(DS3_GAME_HOOKS ${DS3_GAME_HOOKS} PARENT_SCOPE)
    message(STATUS "ds3: ${source} on")
endfunction()
ds3_add_hook(src/ds3_shader_dump.cpp ds3_D3DDevice_CreateVertexShader ds3_D3DDevice_CreatePixelShader
             ds3_XGRegisterVertexShader ds3_XGRegisterPixelShader)
ds3_add_hook(src/ds3_audio.cpp rw__audio__core__MixWithGain rw__audio__core__CopyWithGain)
ds3_setup_game_module(ds3 ds3_deadspace_game_f ${DS3_GAME_HOOKS})

# The version (Homebrew Menu and the first lines of the log) and the commit it was built from, so a log a player sends
# says which build made it. The commit is read when CMake configures (build_mac.sh RECONFIGURE=1 refreshes it).
set(DS3_VERSION "0.0.1 (TU1)")
execute_process(COMMAND git rev-parse --short=10 HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE DS3_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git status --porcelain --untracked-files=no --ignore-submodules=all WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE DS3_SUCIO OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT DS3_COMMIT)
    set(DS3_COMMIT "unknown")
elseif(DS3_SUCIO)
    string(APPEND DS3_COMMIT "+changes")
endif()
target_compile_definitions(ds3 PRIVATE DS3_VERSION="${DS3_VERSION}" DS3_COMMIT="${DS3_COMMIT}")

# Nintendo Switch: ds3.nro. On the SD card: sdmc:/switch/ds3-nx/ds3-nx.nro, with tu/, disc1/ and disc2/ beside it
# (src/ds3_app.h finds them there). No icon from the game: the Homebrew Menu shows the default one.
if(REXGLUE_PLATFORM_SWITCH)
    rexglue_switch_add_nro(ds3
        NAME "Dead Space 3"
        AUTHOR "VasilkovND"
        VERSION "${DS3_VERSION}")
endif()
