# Coverage.cmake
# Minimal helper to enable gcov/llvm coverage on non-MSVC toolchains.

function(enable_coverage target)
  if(MSVC)
    message(WARNING "Coverage is not supported on MSVC in this project")
    return()
  endif()

  if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    message(WARNING "Coverage only supported with GCC/Clang. Compiler: ${CMAKE_C_COMPILER_ID}")
    return()
  endif()

  # Use target-scoped flags so we only instrument tests (and any library they build).
  target_compile_options(${target} PRIVATE -O0 -g --coverage)
  target_link_options(${target} PRIVATE --coverage)

  # Some linkers need libm explicitly when using coverage + cJSON, etc.
  if(UNIX AND NOT APPLE)
    target_link_libraries(${target} PRIVATE m)
  endif()

  # Try to reduce noise and improve path stability in reports.
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  endif()
endfunction()

