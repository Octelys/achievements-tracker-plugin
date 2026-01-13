# EnsureUniversalOpenSSL.cmake
#
# In this project we use OpenSSL APIs (OpenSSL 3.x). For macOS universal builds,
# Homebrew OpenSSL is often single-arch, causing link failures.
#
# Prefer a universal OpenSSL placed at: <source>/.deps/openssl-universal

include_guard(GLOBAL)

if(NOT APPLE)
  return()
endif()

set(_openssl_universal_dir "${CMAKE_CURRENT_SOURCE_DIR}/.deps/openssl-universal")

if(EXISTS "${_openssl_universal_dir}/include/openssl/ssl.h" AND EXISTS "${_openssl_universal_dir}/lib/libcrypto.a")
  message(STATUS "Using universal OpenSSL from ${_openssl_universal_dir}")

  # Force FindOpenSSL to prefer this location.
  set(OPENSSL_ROOT_DIR "${_openssl_universal_dir}" CACHE PATH "Universal OpenSSL root" FORCE)

  # Also push into prefix path for module mode.
  list(PREPEND CMAKE_PREFIX_PATH "${_openssl_universal_dir}")
else()
  message(STATUS "Universal OpenSSL not present at ${_openssl_universal_dir}.")
endif()
