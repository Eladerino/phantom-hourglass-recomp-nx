# Phantom Hourglass: the recompiled game (ph_game, from the player's ROM) and the program (PhantomHourglass).
# Pick the per-region recompiler config from the ROM's 4-byte game code at offset 0x0C (AZEE = USA, AZEP = Europe).
set(PH_CONFIG_CODE "azee")
if(PH_ROM AND EXISTS "${PH_ROM}")
  file(READ "${PH_ROM}" PH_ROM_CODE_HEX OFFSET 12 LIMIT 4 HEX)
  if(PH_ROM_CODE_HEX STREQUAL "415a4550")      # AZEP (Europe)
    set(PH_CONFIG_CODE "azep")
  elseif(PH_ROM_CODE_HEX STREQUAL "415a4545")  # AZEE (USA)
    set(PH_CONFIG_CODE "azee")
  endif()
endif()
set(PH_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/config/${PH_CONFIG_CODE})
if(PH_ROM)
  message(STATUS "Phantom Hourglass config: ${PH_CONFIG_CODE} (${PH_CONFIG})")
endif()

# Embedded HD font (Inter Bold, SIL OFL 1.1) for optional vector text rendering.
set(PH_HD_FONT_SRC ${CMAKE_CURRENT_BINARY_DIR}/hd_font_data.cpp)
add_custom_command(
  OUTPUT ${PH_HD_FONT_SRC}
  COMMAND ${CMAKE_COMMAND}
          -D INPUT=${CMAKE_CURRENT_SOURCE_DIR}/res/fonts/Inter-Bold.ttf
          -D OUTPUT=${PH_HD_FONT_SRC}
          -P ${CMAKE_SOURCE_DIR}/cmake/EmbedFont.cmake
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/fonts/Inter-Bold.ttf ${CMAKE_SOURCE_DIR}/cmake/EmbedFont.cmake
  COMMENT "Embedding HD font (Inter-Bold.ttf)"
  VERBATIM)

# ---- Android: the game's C comes from a PC build (recomp_generated), compiled here for the phone ----
if(ANDROID)
  set(PH_GENERATED_DIR "" CACHE PATH "The recomp_generated folder of a PC build (2-BUILD.ps1)")
  if(NOT EXISTS "${PH_GENERATED_DIR}/func_table.c")
    message(FATAL_ERROR "PH_GENERATED_DIR must be a PC build's recomp_generated folder (run 2-BUILD.ps1 first)")
  endif()
  file(GLOB PH_GEN_SHARDS CONFIGURE_DEPENDS "${PH_GENERATED_DIR}/funcs_*.c")
  add_library(ph_game SHARED ${PH_GENERATED_DIR}/func_table.c ${PH_GEN_SHARDS})
  target_include_directories(ph_game PRIVATE ${PH_GENERATED_DIR} ${CMAKE_SOURCE_DIR}/runtime/include)
  target_compile_definitions(ph_game PRIVATE NTR_GAME_MODULE=1)
  set_target_properties(ph_game PROPERTIES C_VISIBILITY_PRESET hidden)
  target_compile_options(ph_game PRIVATE -w -ffreestanding -fno-stack-protector -O2)
  target_link_options(ph_game PRIVATE -nostdlib)

  # SDL's Android activity loads the program as libmain.so and calls its SDL_main.
  add_library(main SHARED src/main.cpp src/game_module.cpp src/ph_hooks.cpp src/ph_stick.cpp src/ph_temple.cpp src/ph_text.cpp
              ${PH_HD_FONT_SRC} src/png.cpp src/android_bridge.cpp src/gl_present.cpp src/gles_compat.cpp
              src/achievements.cpp)
  target_include_directories(main PRIVATE src ${CMAKE_SOURCE_DIR}/third_party/stb)
  target_compile_options(main PRIVATE -O2)
  target_link_libraries(main PRIVATE ntr_runtime ntr_common SDL3::SDL3-static android log
                        "$<LINK_GROUP:RESCAN,ntr_runtime,melonds_core>")
  add_dependencies(main ph_game)
  return()
endif()

# ---- Switch: the game's C comes from a PC build (recomp_generated), linked statically into the NRO ----
# From ph-switch-builder/builder/switch-fork-skeleton/patches/04-frontend-cmake-switch.snippet.
# UNPROVEN -- no devkitA64 compile was run. SDL3 must come from devkitPro's
# `switch-sdl-3.4` branch (see third_party/CMakeLists.txt TODO); do not use
# the stock FetchContent release-3.4.18 for Switch. -O1 for the game module
# is deliberate (upstream default; the generated C is huge). PH_GENERATED_DIR
# is the host build's recomp_generated/ (the builder passes it:
# cmake --preset switch -DPH_GENERATED_DIR=<build/release/recomp_generated>).
# The `switch` preset must define PH_SWITCH so this branch is taken; the
# top-level CMakeLists.txt must not try to build ntr-recomp with the
# devkitA64 compiler (host tool -- build it with the host presets first, as
# the Android flow does).
if(PH_SWITCH)
  set(PH_GENERATED_DIR "" CACHE PATH "The recomp_generated folder of a PC build (Build-PhantomHourglass-Switch step 4)")
  if(NOT EXISTS "${PH_GENERATED_DIR}/func_table.c")
    message(FATAL_ERROR "PH_GENERATED_DIR must be a PC build's recomp_generated folder (run the builder's host steps first)")
  endif()
  file(GLOB PH_GEN_SHARDS CONFIGURE_DEPENDS "${PH_GENERATED_DIR}/funcs_*.c")
  add_library(ph_game STATIC ${PH_GENERATED_DIR}/func_table.c ${PH_GEN_SHARDS})
  target_include_directories(ph_game PRIVATE ${PH_GENERATED_DIR} ${CMAKE_SOURCE_DIR}/runtime/include)
  target_compile_definitions(ph_game PRIVATE NTR_GAME_MODULE=1)
  set_target_properties(ph_game PROPERTIES C_VISIBILITY_PRESET hidden)
  target_compile_options(ph_game PRIVATE -w -ffreestanding -fno-stack-protector -O1)
  # NOTE: no -nostdlib link option here -- the static archive is linked into
  # the NRO by the program target below; verify against the devkitA64 link
  # before assuming the Android -nostdlib behaviour carries over.

  # (Switch v1: the stub presenter, src/switch_present.cpp — the OpenGL presenter needs a GL loader libnx
  # does not have, and the real presenter is BUILDER-SPEC gap #6. Same GlPresenter interface, so nothing
  # else changes; the game runs with the software renderer but draws nothing yet.)
  add_executable(ph-recomp src/main.cpp src/game_module.cpp src/ph_hooks.cpp src/ph_stick.cpp src/ph_temple.cpp src/ph_text.cpp
               ${PH_HD_FONT_SRC} src/png.cpp src/switch_present.cpp src/menu.cpp src/achievements.cpp)
  target_include_directories(ph-recomp PRIVATE src ${CMAKE_SOURCE_DIR}/third_party/stb)
  target_compile_options(ph-recomp PRIVATE -O2)
  target_link_libraries(ph-recomp PRIVATE ph_game ntr_runtime ntr_common SDL3::SDL3-static nx)

  # ---- NRO packaging (Switch only) ----
  # elf2nro + nacptool are fatal if missing (Fulldex's rule: install
  # switch-dev with dkp-pacman). The .nro target runs on every build.
  # Icon: frontend/res/icon.jpg (256x256 JPEG, generated from the project's
  # own shape-drawn art by tools/gen_icon.py; override with -DPH_SWITCH_ICON).
  # No romfs for v1: the software renderer needs no shaders on the card.
  set(PH_SWITCH_ICON "${CMAKE_CURRENT_SOURCE_DIR}/res/icon.jpg" CACHE FILEPATH "256x256 JPEG icon for the Switch NRO")
  if(NOT EXISTS "${PH_SWITCH_ICON}")
    message(FATAL_ERROR "PH_SWITCH_ICON not found: ${PH_SWITCH_ICON} (generate it with tools/gen_icon.py)")
  endif()
  find_program(PH_ELF2NRO elf2nro PATHS "$ENV{DEVKITPRO}/tools/bin" NO_DEFAULT_PATH)
  find_program(PH_NACPTOOL nacptool PATHS "$ENV{DEVKITPRO}/tools/bin" NO_DEFAULT_PATH)
  if(NOT PH_ELF2NRO OR NOT PH_NACPTOOL)
    message(FATAL_ERROR "elf2nro/nacptool not found in $ENV{DEVKITPRO}/tools/bin (devkitPro switch-dev tools are required for the Switch NRO)")
  endif()
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ph-recomp.nacp
    COMMAND ${PH_NACPTOOL} --create "Phantom Hourglass" "Eladerino" "0.1.0"
            ${CMAKE_CURRENT_BINARY_DIR}/ph-recomp.nacp
    COMMENT "Creating ph-recomp.nacp"
    VERBATIM)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ph-recomp.nro
    COMMAND ${PH_ELF2NRO} $<TARGET_FILE:ph-recomp>
            ${CMAKE_CURRENT_BINARY_DIR}/ph-recomp.nro
            --icon=${PH_SWITCH_ICON} --nacp=${CMAKE_CURRENT_BINARY_DIR}/ph-recomp.nacp
    DEPENDS ph-recomp ${CMAKE_CURRENT_BINARY_DIR}/ph-recomp.nacp ${PH_SWITCH_ICON}
    COMMENT "Packaging ph-recomp.nro"
    VERBATIM)
  add_custom_target(ph-recomp-nro ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/ph-recomp.nro)
  # Presenter: v1 is the stub (src/switch_present.cpp) — the game runs with the software renderer but draws
  # nothing yet. The real presenter (spec section 5.2 gap #6: Mesa GLES or a thin deko3d presenter showing
  # the software renderer's frames) replaces the stub by swapping the one source above back to gl_present.cpp.
  return()
endif()

# ---- the recompiled game: built only with PH_ROM (it is generated from the player's own ROM, never committed) ----
if(PH_ROM)
  set(PH_GEN_DIR ${CMAKE_BINARY_DIR}/recomp_generated)
  # ntr-recomp writes func_table.c and 64 shards funcs_000.c .. funcs_063.c (plus funcs.h).
  set(PH_GEN_SOURCES ${PH_GEN_DIR}/func_table.c)
  foreach(i RANGE 63)
    if(i LESS 10)
      set(n "00${i}")
    else()
      set(n "0${i}")
    endif()
    list(APPEND PH_GEN_SOURCES ${PH_GEN_DIR}/funcs_${n}.c)
  endforeach()

  add_custom_command(
    OUTPUT ${PH_GEN_SOURCES} ${PH_GEN_DIR}/funcs.h
    COMMAND ntr-recomp --rom "${PH_ROM}" --config "${PH_CONFIG}" --out "${PH_GEN_DIR}"
            --report "${CMAKE_BINARY_DIR}/recomp_report.txt"
    DEPENDS ntr-recomp ${PH_CONFIG}/functions.txt ${PH_CONFIG}/hooks.txt ${PH_CONFIG}/regions.txt "${PH_ROM}"
    COMMENT "Recompiling the game's code from your ROM"
    VERBATIM)
  # Only the C (what the Android build needs): cmake --build --preset release --target ph_generate
  add_custom_target(ph_generate DEPENDS ${PH_GEN_SOURCES} ${PH_GEN_DIR}/funcs.h)

  # A freestanding shared library: the runtime's services reach it through a table when it is loaded (ntr/recomp.h).
  add_library(ph_game MODULE ${PH_GEN_SOURCES})
  target_include_directories(ph_game PRIVATE ${PH_GEN_DIR} ${CMAKE_SOURCE_DIR}/runtime/include)
  target_compile_definitions(ph_game PRIVATE NTR_GAME_MODULE=1)
  set_target_properties(ph_game PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    C_VISIBILITY_PRESET hidden)
  set(PH_GAME_OPT "" CACHE STRING "Optimization flag for the recompiled game (default /O1 or -O1)")
  if(MSVC)
    if(NOT PH_GAME_OPT)
      set(PH_GAME_OPT /O1)
    endif()
    target_compile_options(ph_game PRIVATE /w /GS- /Zl ${PH_GAME_OPT})
    target_link_options(ph_game PRIVATE /NODEFAULTLIB /NOENTRY)
  else()
    if(NOT PH_GAME_OPT)
      set(PH_GAME_OPT -O1)
    endif()
    target_compile_options(ph_game PRIVATE -w -ffreestanding -fno-stack-protector ${PH_GAME_OPT})
    target_link_options(ph_game PRIVATE -nostdlib)
  endif()
else()
  message(STATUS "PH_ROM not set: building the program only (it needs ph_game next to it to play)")
endif()

# ---- the program ----
add_executable(PhantomHourglass src/main.cpp src/game_module.cpp src/ph_hooks.cpp src/ph_stick.cpp src/ph_temple.cpp src/ph_text.cpp
               ${PH_HD_FONT_SRC} src/png.cpp src/gl_present.cpp src/menu.cpp src/achievements.cpp)
target_include_directories(PhantomHourglass PRIVATE src ${CMAKE_SOURCE_DIR}/third_party/stb)
set_target_properties(PhantomHourglass PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
if(PH_ROM)
  add_dependencies(PhantomHourglass ph_game)  # (built together, next to the program)
endif()
target_link_libraries(PhantomHourglass PRIVATE ntr_runtime ntr_common SDL3::SDL3-static)
if(PH_GLES_SHADERS)  # (the gles-test preset: the phone's renderer, its ES shaders and gles_compat, on the PC)
  target_sources(PhantomHourglass PRIVATE src/gles_compat.cpp)
  target_compile_definitions(PhantomHourglass PRIVATE PH_GLES_TEST=1)
endif()
if(NOT MSVC)
  # melonDS calls the Platform:: functions ntr_runtime defines and ntr_runtime calls melonDS: rescan them (GNU ld).
  target_link_libraries(PhantomHourglass PRIVATE "$<LINK_GROUP:RESCAN,ntr_runtime,melonds_core>")
endif()
if(WIN32)
  enable_language(RC)
  target_sources(PhantomHourglass PRIVATE app.rc)
  set_source_files_properties(app.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/app.ico)
  target_link_libraries(PhantomHourglass PRIVATE opengl32 user32 advapi32 shell32 ws2_32 iphlpapi)
  # A windowed program (no console window behind it); its output still goes to a redirected stdout/stderr.
  if(MSVC)
    target_link_options(PhantomHourglass PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
  else()
    target_link_options(PhantomHourglass PRIVATE -mwindows)
  endif()
endif()
