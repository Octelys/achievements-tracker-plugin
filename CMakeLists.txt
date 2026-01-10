cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

# The plugin target must exist before we can attach sources/libraries to it.
# The macOS helpers set the bundle properties later via `set_target_properties_plugin()`.
add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

# HTTP client for OAuth + XBL/XSTS flows
find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)

# OpenSSL
#
# IMPORTANT (macOS universal): do NOT link against Homebrew's OpenSSL.
# On arm64 runners, Homebrew's openssl bottle is arm64-only, which breaks
# universal (arm64+x86_64) linking.
#
# obs-deps provides a universal OpenSSL under `.deps`, and buildspec bootstrap
# will download it and add it to CMAKE_PREFIX_PATH. We prevent CMake from
# searching in Homebrew paths.
if(APPLE)
  # Ignore typical Homebrew prefixes to prevent finding arm64-only OpenSSL
  list(APPEND CMAKE_IGNORE_PREFIX_PATH "/opt/homebrew" "/usr/local")

  # For debugging: show CMAKE_PREFIX_PATH
  message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")

  # Search for OpenSSL logic:
  # 1. Try to find openssl/ssl.h recursively in CMAKE_PREFIX_PATH to handle non-standard layouts.
  # 2. If found, set OPENSSL_ROOT_DIR accordingly.
  # 3. If not found, do NOT set OPENSSL_ROOT_DIR to a missing path, let find_package search.

  set(_found_openssl_root "")
  if(CMAKE_PREFIX_PATH)
    foreach(_prefix ${CMAKE_PREFIX_PATH})
      message(STATUS "Searching for OpenSSL in prefix: ${_prefix}")
      file(GLOB_RECURSE _ssl_headers "${_prefix}/openssl/ssl.h")
      foreach(_header ${_ssl_headers})
        # _header is like /path/to/prefix/.../include/openssl/ssl.h
        # We need the root, e.g. /path/to/prefix/...
        # So we go up 3 levels: ssl.h -> openssl -> include -> ROOT
        get_filename_component(_inc_dir "${_header}" DIRECTORY) # .../openssl
        get_filename_component(_inc_dir "${_inc_dir}" DIRECTORY) # .../include (maybe)

        # Check if it ends with "include"
        if(_inc_dir MATCHES "/include$")
           get_filename_component(_root "${_inc_dir}" DIRECTORY)
           set(_found_openssl_root "${_root}")
           message(STATUS "Found OpenSSL root via header search: ${_found_openssl_root}")
           break()
        endif()
      endforeach()
      if(_found_openssl_root)
        break()
      endif()
    endforeach()
  endif()

  if(_found_openssl_root)
    set(OPENSSL_ROOT_DIR "${_found_openssl_root}")
  else()
    message(STATUS "OpenSSL headers not found in obs-deps. Relying on default search (skipping Homebrew).")
    # Do NOT prevent find_package from searching by setting an invalid OPENSSL_ROOT_DIR
  endif()
endif()

# On macOS, obs-deps doesn't provide OpenSSLConfig.cmake, so skip CONFIG mode
# On other platforms, try CONFIG mode first
if(NOT APPLE)
  find_package(OpenSSL CONFIG QUIET)
endif()

if(NOT TARGET OpenSSL::Crypto)
  # Use module mode (FindOpenSSL.cmake)
  if(APPLE)
    # On macOS, only search in CMAKE_PREFIX_PATH (not system paths or Homebrew)
    # We rely on CMAKE_IGNORE_PREFIX_PATH to skip Homebrew.
    # obs-deps provides OpenSSL libraries.
    find_package(OpenSSL REQUIRED)
  else()
    find_package(OpenSSL REQUIRED)
  endif()
endif()

if(TARGET OpenSSL::Crypto)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OpenSSL::Crypto)
  if(APPLE)
    message(STATUS "Successfully linked OpenSSL::Crypto target")
  endif()
else()
  # Module mode found it but didn't create imported target - verify and link manually
  if(APPLE)
    message(STATUS "Found OpenSSL at: ${OPENSSL_CRYPTO_LIBRARY}")

    if(OPENSSL_CRYPTO_LIBRARY MATCHES "(/opt/homebrew|/usr/local)")
      message(FATAL_ERROR
        "Found Homebrew's OpenSSL at ${OPENSSL_CRYPTO_LIBRARY} which is arm64-only.\n"
        "This breaks universal (arm64+x86_64) builds.\n"
        "The buildspec system should have downloaded universal OpenSSL from obs-deps.\n"
        "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}\n"
        "If you see this error, Homebrew's OpenSSL may need to be uninstalled.")
    endif()
  endif()

  target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${OPENSSL_INCLUDE_DIR})
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
endif()

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# Logging helpers (obs_log wrapper + plugin name/version)
if(TARGET diagnostics-log)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE diagnostics-log)
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  src/main.c
  src/sources/achievements-tracker-source.c
  src/configuration/properties.c
  src/crypto/crypto.c
  src/net/browser/browser.c
  src/net/http/http.c
  src/net/json/json.c
  src/oauth/callback-server.c
  src/oauth/util.c
  src/oauth/xbox-live.c
  src/xbox/client.c
  src/io/state.c
  src/encoding/encoder.c
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
