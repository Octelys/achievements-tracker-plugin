# EnsureUniversalFreeType.cmake
#
# In this project we use FreeType for text rendering. For macOS universal builds,
# Homebrew FreeType is often single-arch, causing link failures.
#
# Prefer a universal FreeType placed at: <source>/.deps/freetype-universal

include_guard(GLOBAL)

if(NOT APPLE)
  return()
endif()

set(_freetype_universal_dir "${CMAKE_CURRENT_SOURCE_DIR}/.deps/freetype-universal")

if(
  EXISTS "${_freetype_universal_dir}/include/freetype2/ft2build.h"
  AND
    (EXISTS "${_freetype_universal_dir}/lib/libfreetype.a" OR EXISTS "${_freetype_universal_dir}/lib/libfreetype.dylib")
)
  message(STATUS "Using universal FreeType from ${_freetype_universal_dir}")

  # Force FindFreetype to prefer this location.
  set(FREETYPE_DIR "${_freetype_universal_dir}" CACHE PATH "Universal FreeType root" FORCE)

  # Also push into prefix path for module mode.
  list(PREPEND CMAKE_PREFIX_PATH "${_freetype_universal_dir}")
else()
  message(STATUS "Universal FreeType not present at ${_freetype_universal_dir}.")
endif()
