# ntr runtime: services for recompiled ARM9 code on top of the melonDS core.
add_library(ntr_runtime STATIC
  src/runtime.cpp
  src/swi.cpp
  src/nitro.cpp
  src/fiber.cpp
  src/gx_match.cpp
  src/gx_stream.cpp
  src/melon_platform.cpp
  src/savestate.cpp)
# (gl_between.cpp is the GL renderer's frame interpolation: it names GLRenderer members and glad functions, and
# the OpenGL renderer is not built on Switch. The runtime never calls it there — display_rate is always off
# with the software renderer — so it is left out of the Switch library entirely.)
if(NOT PH_SWITCH)
  target_sources(ntr_runtime PRIVATE src/gl_between.cpp)
endif()
target_include_directories(ntr_runtime PUBLIC include PRIVATE src)
target_link_libraries(ntr_runtime PUBLIC melonds_core ntr_common)
