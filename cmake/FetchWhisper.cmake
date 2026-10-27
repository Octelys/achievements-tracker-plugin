include(FetchContent)

# Pinned for reproducibility. whisper.cpp bundles ggml and produces a `whisper`
# CMake target; we only need the library, not its examples/tests/servers.
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ggml's Metal backend is hand-written Objective-C using manual reference
# counting; this project enables ARC for every target project-wide (Xcode
# generator applies CLANG_ENABLE_OBJC_ARC globally), which breaks it. The
# CPU/BLAS backends ggml falls back to are plenty fast for short chat
# utterances on a tiny/base whisper model, so just skip Metal.
set(GGML_METAL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  whispercpp
  GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
  GIT_TAG v1.7.4
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(whispercpp)

# whisper.cpp/ggml are vendored third-party code we don't own the warnings
# for; this project's own build settings (strict warnings-as-errors, applied
# globally to every target under the Xcode generator) would otherwise break
# the build on toolchain/warning-set differences we can't control upstream.
function(disable_warnings_as_errors_recursive dir)
  get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_target IN LISTS _targets)
    set_target_properties(${_target} PROPERTIES
      XCODE_ATTRIBUTE_GCC_TREAT_WARNINGS_AS_ERRORS NO
      COMPILE_WARNING_AS_ERROR OFF
    )
    target_compile_options(${_target} PRIVATE -Wno-error)
  endforeach()

  get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(_subdir IN LISTS _subdirs)
    disable_warnings_as_errors_recursive(${_subdir})
  endforeach()
endfunction()

disable_warnings_as_errors_recursive(${whispercpp_SOURCE_DIR})
