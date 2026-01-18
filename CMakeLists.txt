cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

# Prefer universal OpenSSL when present (macOS)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EnsureUniversalOpenSSL.cmake")

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

# ------------------------------
# Vendored deps
# ------------------------------
# cJSON (and optional cJSON_Utils)
add_library(cjson STATIC external/cjson/cJSON.c external/cjson/cJSON_Utils.c)

target_include_directories(cjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/external/cjson)

# Windows UUID (Rpc.h/UUuid*) lives in Rpcrt4
if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Rpcrt4)
endif()

# UUID (Linux/BSD): uuid_generate_random/uuid_unparse_lower live in libuuid
if(NOT WIN32)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBUUID QUIET uuid)
  endif()

  if(LIBUUID_FOUND)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${LIBUUID_INCLUDE_DIRS})
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${LIBUUID_LIBRARIES})
  else()
    # Fallback: most Linux distros provide libuuid as -luuid
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE uuid)
  endif()
endif()

# HTTP client for OAuth + XBL/XSTS flows
find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)

# WebSocket client for Xbox Live RTA monitoring
# Try multiple methods to find libwebsockets
set(LIBWEBSOCKETS_FOUND FALSE)

# Method 1: Try CMake's find_package
find_package(Libwebsockets QUIET)
if(Libwebsockets_FOUND OR LIBWEBSOCKETS_FOUND)
  set(LIBWEBSOCKETS_FOUND TRUE)
  if(TARGET websockets OR TARGET websockets_shared)
    # Use the imported target
    if(TARGET websockets_shared)
      target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE websockets_shared)
    else()
      target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE websockets)
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_LIBWEBSOCKETS)
    message(STATUS "Found libwebsockets via CMake package")
  endif()
endif()

# Method 2: Try pkg-config if CMake method didn't work
if(NOT LIBWEBSOCKETS_FOUND)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBWEBSOCKETS QUIET libwebsockets)
    if(LIBWEBSOCKETS_FOUND)
      target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS})
      target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${LIBWEBSOCKETS_LIBRARIES})
      target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_LIBWEBSOCKETS)
      message(STATUS "Found libwebsockets via pkg-config")
    endif()
  endif()
endif()

# Method 3: Try manual search in common locations (Homebrew, etc.)
if(NOT LIBWEBSOCKETS_FOUND AND APPLE)
  # Check common Homebrew paths
  set(HOMEBREW_PREFIXES "/opt/homebrew" "/usr/local")
  foreach(prefix ${HOMEBREW_PREFIXES})
    if(EXISTS "${prefix}/include/libwebsockets.h")
      if(EXISTS "${prefix}/lib/libwebsockets.dylib" OR EXISTS "${prefix}/lib/libwebsockets.a")
        target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${prefix}/include")
        target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE "${prefix}/lib")
        target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE websockets)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_LIBWEBSOCKETS)
        set(LIBWEBSOCKETS_FOUND TRUE)
        message(STATUS "Found libwebsockets in ${prefix}")
        break()
      endif()
    endif()
  endforeach()
endif()

if(NOT LIBWEBSOCKETS_FOUND)
  message(WARNING "libwebsockets not found. Xbox Live RTA monitoring will be disabled.")
  message(WARNING "To enable RTA monitoring, install libwebsockets:")
  message(WARNING "  macOS: brew install libwebsockets")
  message(WARNING "  Linux: apt-get install libwebsockets-dev (or dnf install libwebsockets-devel)")
endif()

if(APPLE)
  execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-path
    OUTPUT_VARIABLE MACOS_SDK_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -isysroot ${MACOS_SDK_PATH}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isysroot ${MACOS_SDK_PATH}")
endif()

# OpenSSL
#
# IMPORTANT (macOS universal): do NOT link against Homebrew's OpenSSL.
# On arm64 runners, Homebrew's openssl bottle is arm64-only, which breaks
# universal (arm64+x86_64) linking.
#
# obs-deps 2024+ no longer includes OpenSSL. We build a universal OpenSSL
# in .deps/openssl-universal during the CI build.
if(APPLE)
  # Ignore typical Homebrew prefixes to prevent finding arm64-only OpenSSL
  list(APPEND CMAKE_IGNORE_PREFIX_PATH "/opt/homebrew" "/usr/local")
endif()

# Prefer CONFIG mode on non-macOS.
if(NOT APPLE)
  find_package(OpenSSL CONFIG QUIET)
endif()

if(NOT TARGET OpenSSL::Crypto)
  find_package(OpenSSL REQUIRED)
endif()

# If OpenSSL resolves to Homebrew on macOS, fail early ONLY for universal builds.
# Homebrew bottles are often single-arch (arm64 on Apple Silicon Macs), which
# breaks universal (arm64+x86_64) builds. For single-arch dev builds, allow it.
if(APPLE)
  list(LENGTH CMAKE_OSX_ARCHITECTURES _arch_count)
  if(_arch_count GREATER 1)
    # Universal build - require non-Homebrew OpenSSL
    if(OPENSSL_CRYPTO_LIBRARY MATCHES "(/opt/homebrew|/usr/local)")
      message(
        FATAL_ERROR
        "Found Homebrew's OpenSSL at ${OPENSSL_CRYPTO_LIBRARY} which is often single-arch.\n"
        "This breaks universal (arm64+x86_64) builds.\n"
        "Install/use a universal OpenSSL (CI builds one at .deps/openssl-universal).\n"
        "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}\n"
        "For local testing, use the 'macos-dev' preset which builds for native arch only."
      )
    endif()
  else()
    # Single-arch build - Homebrew OpenSSL is fine
    if(OPENSSL_CRYPTO_LIBRARY MATCHES "(/opt/homebrew|/usr/local)")
      message(STATUS "Using Homebrew OpenSSL for single-arch dev build: ${OPENSSL_CRYPTO_LIBRARY}")
    endif()
  endif()
endif()

if(TARGET OpenSSL::Crypto)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OpenSSL::Crypto)
  if(APPLE)
    message(STATUS "Successfully linked OpenSSL::Crypto target")
  endif()
else()
  # Module mode found it but didn't create imported target - link manually
  if(APPLE)
    message(STATUS "Found OpenSSL at: ${OPENSSL_CRYPTO_LIBRARY}")

    # Only fail on Homebrew for universal builds
    list(LENGTH CMAKE_OSX_ARCHITECTURES _arch_count2)
    if(_arch_count2 GREATER 1 AND OPENSSL_CRYPTO_LIBRARY MATCHES "(/opt/homebrew|/usr/local)")
      message(
        FATAL_ERROR
        "Found Homebrew's OpenSSL at ${OPENSSL_CRYPTO_LIBRARY} which is arm64-only.\n"
        "This breaks universal (arm64+x86_64) builds.\n"
        "The buildspec system should have downloaded universal OpenSSL from obs-deps.\n"
        "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}\n"
        "For local testing, use the 'macos-dev' preset which builds for native arch only."
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
    src/sources/xbox/account.c
    src/sources/xbox/game_cover.c
    src/sources/xbox/gamerscore.c
    src/crypto/crypto.c
    src/common/types.c
    src/drawing/image.c
    src/drawing/text.c
    src/net/browser/browser.c
    src/net/http/http.c
    src/net/json/json.c
    src/oauth/util.c
    src/oauth/xbox-live.c
    src/xbox/xbox_client.c
    src/xbox/xbox_monitor.c
    src/io/state.c
    src/encoding/base64.c
    src/util/uuid.c
    src/text/parsers.c
    src/time/time.c
)

# Link vendored deps
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE cjson)
if(UNIX AND NOT APPLE)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE m)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# ------------------------------
# Unit tests (Unity)
# ------------------------------
include(CTest)

if(BUILD_TESTING)
  # Unity test framework (vendored via FetchContent)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchUnity.cmake)

  # ------------------------------
  # test_encoder
  # ------------------------------
  add_executable(
    test_encoder
    test/test_encoder.c
    ${unity_SOURCE_DIR}/src/unity.c
    src/encoding/base64.c
    src/util/uuid.c
    src/time/time.c
    test/stubs/bmem_stub.c
  )

  target_include_directories(
    test_encoder
    PRIVATE
      # Use stub headers instead of real OBS headers for unit tests
      ${CMAKE_CURRENT_SOURCE_DIR}/test/stubs
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${unity_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/test
  )

  # Unity uses UNITY_INCLUDE_CONFIG_H to include a project-specific config header.
  target_compile_definitions(test_encoder PRIVATE UNITY_INCLUDE_CONFIG_H)

  # Link vendored deps
  target_link_libraries(test_encoder PRIVATE cjson)
  if(UNIX AND NOT APPLE)
    target_link_libraries(test_encoder PRIVATE m)
  endif()

  if(WIN32)
    target_link_libraries(test_encoder PRIVATE Rpcrt4)
  else()
    # libuuid for uuid_generate_random/uuid_unparse_lower
    if(LIBUUID_FOUND)
      target_include_directories(test_encoder PRIVATE ${LIBUUID_INCLUDE_DIRS})
      target_link_libraries(test_encoder PRIVATE ${LIBUUID_LIBRARIES})
    else()
      target_link_libraries(test_encoder PRIVATE uuid)
    endif()
  endif()

  # Use the same OpenSSL linkage policy as the plugin target (universal-safe on macOS).
  if(TARGET OpenSSL::Crypto)
    target_link_libraries(test_encoder PRIVATE OpenSSL::Crypto)
    if(TARGET OpenSSL::SSL)
      target_link_libraries(test_encoder PRIVATE OpenSSL::SSL)
    endif()

    # Ensure headers are available even if the imported targets don't propagate include dirs.
    if(OPENSSL_INCLUDE_DIR)
      target_include_directories(test_encoder PRIVATE ${OPENSSL_INCLUDE_DIR})
    endif()
  else()
    target_include_directories(test_encoder PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(test_encoder PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
    if(OPENSSL_SSL_LIBRARY)
      target_link_libraries(test_encoder PRIVATE ${OPENSSL_SSL_LIBRARY})
    endif()
  endif()

  # ------------------------------
  # test_crypto
  # ------------------------------
  add_executable(
    test_crypto
    test/test_crypto.c
    ${unity_SOURCE_DIR}/src/unity.c
    src/crypto/crypto.c
    src/encoding/base64.c
    src/net/json/json.c
    src/util/uuid.c
    src/time/time.c
    test/stubs/bmem_stub.c
  )

  target_include_directories(
    test_crypto
    PRIVATE
      # Use stub headers instead of real OBS headers for unit tests
      ${CMAKE_CURRENT_SOURCE_DIR}/test/stubs
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${unity_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/test
  )

  target_compile_definitions(test_crypto PRIVATE UNITY_INCLUDE_CONFIG_H)

  # Link vendored deps
  target_link_libraries(test_crypto PRIVATE cjson)
  if(UNIX AND NOT APPLE)
    target_link_libraries(test_crypto PRIVATE m)
  endif()

  if(WIN32)
    target_link_libraries(test_crypto PRIVATE Rpcrt4)
  else()
    if(LIBUUID_FOUND)
      target_include_directories(test_crypto PRIVATE ${LIBUUID_INCLUDE_DIRS})
      target_link_libraries(test_crypto PRIVATE ${LIBUUID_LIBRARIES})
    else()
      target_link_libraries(test_crypto PRIVATE uuid)
    endif()
  endif()

  # OpenSSL is required by test_crypto (EVP/BIO/BN/ECDSA helpers).
  if(TARGET OpenSSL::Crypto)
    target_link_libraries(test_crypto PRIVATE OpenSSL::Crypto)
    if(TARGET OpenSSL::SSL)
      target_link_libraries(test_crypto PRIVATE OpenSSL::SSL)
    endif()

    if(OPENSSL_INCLUDE_DIR)
      target_include_directories(test_crypto PRIVATE ${OPENSSL_INCLUDE_DIR})
    endif()
  else()
    target_include_directories(test_crypto PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(test_crypto PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
    if(OPENSSL_SSL_LIBRARY)
      target_link_libraries(test_crypto PRIVATE ${OPENSSL_SSL_LIBRARY})
    endif()
  endif()

  # ------------------------------
  # test_time
  # ------------------------------
  add_executable(test_time test/test_time.c ${unity_SOURCE_DIR}/src/unity.c src/time/time.c test/stubs/bmem_stub.c)

  target_include_directories(
    test_time
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/test/stubs
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${unity_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/test
  )

  target_compile_definitions(test_time PRIVATE UNITY_INCLUDE_CONFIG_H)

  # Link vendored deps
  target_link_libraries(test_time PRIVATE cjson)
  if(UNIX AND NOT APPLE)
    target_link_libraries(test_time PRIVATE m)
  endif()

  # Use the same OpenSSL linkage policy as the plugin target (universal-safe on macOS).
  if(TARGET OpenSSL::Crypto)
    target_link_libraries(test_time PRIVATE OpenSSL::Crypto)
    if(TARGET OpenSSL::SSL)
      target_link_libraries(test_time PRIVATE OpenSSL::SSL)
    endif()

    # Ensure headers are available even if the imported targets don't propagate include dirs.
    if(OPENSSL_INCLUDE_DIR)
      target_include_directories(test_time PRIVATE ${OPENSSL_INCLUDE_DIR})
    endif()
  else()
    target_include_directories(test_time PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(test_time PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
    if(OPENSSL_SSL_LIBRARY)
      target_link_libraries(test_time PRIVATE ${OPENSSL_SSL_LIBRARY})
    endif()
  endif()

  # ------------------------------
  # test_parsers
  # ------------------------------
  add_executable(
    test_parsers
    test/test_parsers.c
    ${unity_SOURCE_DIR}/src/unity.c
    src/text/parsers.c
    test/stubs/bmem_stub.c
  )

  target_include_directories(
    test_parsers
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/test/stubs
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${unity_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/test
  )

  target_compile_definitions(test_parsers PRIVATE UNITY_INCLUDE_CONFIG_H)

  # Link vendored deps
  target_link_libraries(test_parsers PRIVATE cjson)
  if(UNIX AND NOT APPLE)
    target_link_libraries(test_parsers PRIVATE m)
  endif()

  # Use the same OpenSSL linkage policy as the plugin target (universal-safe on macOS).
  if(TARGET OpenSSL::Crypto)
    target_link_libraries(test_parsers PRIVATE OpenSSL::Crypto)
    if(TARGET OpenSSL::SSL)
      target_link_libraries(test_parsers PRIVATE OpenSSL::SSL)
    endif()

    # Ensure headers are available even if the imported targets don't propagate include dirs.
    if(OPENSSL_INCLUDE_DIR)
      target_include_directories(test_parsers PRIVATE ${OPENSSL_INCLUDE_DIR})
    endif()
  else()
    target_include_directories(test_parsers PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(test_parsers PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
    if(OPENSSL_SSL_LIBRARY)
      target_link_libraries(test_parsers PRIVATE ${OPENSSL_SSL_LIBRARY})
    endif()
  endif()
endif()
