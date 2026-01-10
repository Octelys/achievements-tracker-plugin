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
# obs-deps 2024+ no longer includes OpenSSL. We build a universal OpenSSL
# in .deps/openssl-universal during the CI build. We prevent CMake from
# searching in Homebrew paths to avoid arm64-only libraries.
if(APPLE)
  # Ignore typical Homebrew prefixes to prevent finding arm64-only OpenSSL
  list(APPEND CMAKE_IGNORE_PREFIX_PATH "/opt/homebrew" "/usr/local")

  # For debugging: show CMAKE_PREFIX_PATH
  message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")

  set(_found_openssl_root "")

  # First, check for our custom-built universal OpenSSL in .deps/openssl-universal
  set(_openssl_universal_dir "${CMAKE_CURRENT_SOURCE_DIR}/.deps/openssl-universal")
  if(EXISTS "${_openssl_universal_dir}/include/openssl/ssl.h")
    message(STATUS "Found universal OpenSSL at: ${_openssl_universal_dir}")
    set(OPENSSL_ROOT_DIR "${_openssl_universal_dir}")
    set(_found_openssl_root "${_openssl_universal_dir}")
  else()
    # Try to find openssl/ssl.h in various locations within CMAKE_PREFIX_PATH
    find_path(
      _openssl_include_dir
      openssl/ssl.h
      PATHS ${CMAKE_PREFIX_PATH}
      PATH_SUFFIXES include ssl/include openssl/include
      NO_DEFAULT_PATH
    )

    if(_openssl_include_dir)
      get_filename_component(_root "${_openssl_include_dir}" DIRECTORY)
      set(_found_openssl_root "${_root}")
      message(STATUS "Found OpenSSL root via find_path: ${_found_openssl_root}")
      set(OPENSSL_ROOT_DIR "${_found_openssl_root}")
    else()
      message(STATUS "OpenSSL headers not found in obs-deps or .deps/openssl-universal.")

      # Debug: list contents of obs-deps directories to help diagnose
      foreach(_prefix ${CMAKE_PREFIX_PATH})
        if(_prefix MATCHES "obs-deps" AND NOT _prefix MATCHES "qt")
          message(STATUS "Checking obs-deps directory: ${_prefix}")
          if(EXISTS "${_prefix}/include")
            message(STATUS "  include/ exists")
          endif()
          if(EXISTS "${_prefix}/lib")
            message(STATUS "  lib/ exists")
          endif()
          if(EXISTS "${_prefix}/include/openssl")
            message(STATUS "  include/openssl/ exists - OpenSSL headers found!")
            set(OPENSSL_ROOT_DIR "${_prefix}")
            set(_found_openssl_root "${_prefix}")
          endif()
        endif()
      endforeach()

      if(NOT _found_openssl_root)
        message(STATUS "OpenSSL not found. Will search system paths (excluding Homebrew).")
      endif()
    endif()
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
      message(
        FATAL_ERROR
        "Found Homebrew's OpenSSL at ${OPENSSL_CRYPTO_LIBRARY} which is arm64-only.\n"
        "This breaks universal (arm64+x86_64) builds.\n"
        "The buildspec system should have downloaded universal OpenSSL from obs-deps.\n"
        "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}\n"
        "If you see this error, Homebrew's OpenSSL may need to be uninstalled."
      )
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

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
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
