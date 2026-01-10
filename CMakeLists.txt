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
  message(DEBUG "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
endif()

# Try config mode first (creates OpenSSL::Crypto target)
find_package(OpenSSL CONFIG QUIET)

if(NOT TARGET OpenSSL::Crypto)
  # Fallback to module mode
  if(APPLE)
    # On macOS, search in CMAKE_PREFIX_PATH but not system environment paths
    # NO_SYSTEM_ENVIRONMENT_PATH prevents finding Homebrew via PKG_CONFIG or other env vars
    # NO_CMAKE_SYSTEM_PATH prevents searching /usr/local etc.
    find_package(OpenSSL REQUIRED
      NO_SYSTEM_ENVIRONMENT_PATH
      NO_CMAKE_SYSTEM_PATH)
  else()
    find_package(OpenSSL REQUIRED)
  endif()
endif()

if(TARGET OpenSSL::Crypto)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OpenSSL::Crypto)
  if(APPLE)
    message(STATUS "Using OpenSSL (CONFIG mode)")
  endif()
else()
  # Module mode found it - verify it's not from Homebrew on macOS
  if(APPLE)
    message(STATUS "Using OpenSSL from: ${OPENSSL_CRYPTO_LIBRARY} (MODULE mode)")

    if(OPENSSL_CRYPTO_LIBRARY MATCHES "(/opt/homebrew|/usr/local)")
      message(FATAL_ERROR
        "Found Homebrew's OpenSSL at ${OPENSSL_CRYPTO_LIBRARY} which is arm64-only.\n"
        "This breaks universal (arm64+x86_64) builds.\n"
        "The buildspec system should have downloaded universal OpenSSL from obs-deps.\n"
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
