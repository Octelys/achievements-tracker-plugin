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
  # Note: we intentionally use gcov-style coverage here (works with GCC and Clang).
  target_compile_options(${target} PRIVATE -O0 -g --coverage)
  target_link_options(${target} PRIVATE --coverage)

  # Some linkers need libm explicitly when using coverage + cJSON, etc.
  if(UNIX AND NOT APPLE)
    target_link_libraries(${target} PRIVATE m)
  endif()
endfunction()

# Adds a convenience build target `coverage` which:
#   1) builds the test targets
#   2) runs ctest
#   3) generates an HTML report under <binary_dir>/coverage/html (if gcovr is installed)
# Usage: add_coverage_target(test_target1 test_target2 ...)
function(add_coverage_target)
  if(NOT ENABLE_COVERAGE)
    return()
  endif()

  if(MSVC)
    message(STATUS "Coverage target not created: MSVC")
    return()
  endif()

  # All arguments are test target names that coverage depends on
  set(TEST_TARGETS ${ARGN})

  # Find gcovr (optional). If missing, we still provide a 'coverage' target that runs tests.
  find_program(GCOVR_EXECUTABLE gcovr)

  if(GCOVR_EXECUTABLE)
    add_custom_target(
      coverage
      COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage/html
      COMMAND
        ${GCOVR_EXECUTABLE} -r ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} --exclude ${CMAKE_SOURCE_DIR}/external/ --exclude
        ${CMAKE_SOURCE_DIR}/test/ --exclude ${CMAKE_BINARY_DIR}/_deps/ --html --html-details -o
        ${CMAKE_BINARY_DIR}/coverage/html/index.html
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      DEPENDS ${TEST_TARGETS}
      COMMENT "Running unit tests and generating HTML coverage report"
      VERBATIM
    )
  else()
    add_custom_target(
      coverage
      COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      DEPENDS ${TEST_TARGETS}
      COMMENT "Running unit tests (install 'gcovr' to also generate an HTML coverage report)"
      VERBATIM
    )
  endif()
endfunction()
