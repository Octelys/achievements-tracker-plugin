cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

# HTTP client for OAuth + XBL/XSTS flows
find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)

# OpenSSL
if(APPLE)
  if(EXISTS /opt/homebrew/opt/openssl@3/lib/cmake/OpenSSL)
    set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl@3)
    set(OpenSSL_DIR /opt/homebrew/opt/openssl@3/lib/cmake/OpenSSL)
  elseif(EXISTS /usr/local/opt/openssl@3/lib/cmake/OpenSSL)
    set(OPENSSL_ROOT_DIR /usr/local/opt/openssl@3)
    set(OpenSSL_DIR /usr/local/opt/openssl@3/lib/cmake/OpenSSL)
  endif()
endif()

# Prefer config mode so OpenSSL::Crypto exists
find_package(OpenSSL CONFIG QUIET)

if(TARGET OpenSSL::Crypto)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OpenSSL::Crypto)
else()
  # Fallback to module mode (no imported targets)
  find_package(OpenSSL REQUIRED)
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
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
