cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

option(ENABLE_COVERAGE "Enable code coverage instrumentation for unit tests (GCC/Clang)" OFF)
if(ENABLE_COVERAGE)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Coverage.cmake")
endif()

# Prefer universal OpenSSL when present (macOS)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EnsureUniversalOpenSSL.cmake")

# Prefer universal FreeType when present (macOS)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EnsureUniversalFreeType.cmake")

project(${_name} VERSION ${PLUGIN_VERSION})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

if(NOT ENABLE_FRONTEND_API)
  message(STATUS "Enabling obs-frontend-api because the global Xbox account configuration requires it")
  set(ENABLE_FRONTEND_API ON CACHE BOOL "Use obs-frontend-api for UI functionality" FORCE)
  set(ENABLE_FRONTEND_API ON)
endif()

if(NOT ENABLE_QT)
  message(STATUS "Enabling Qt because the global Xbox account configuration requires it")
  set(ENABLE_QT ON CACHE BOOL "Use Qt functionality" FORCE)
  set(ENABLE_QT ON)
endif()

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

# HTTP client for OAuth + XBL/XSTS flowsmessage(STATUS "Found cURL version: ${CURL_VERSION_STRING}")
find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)

# WebSocket client for Xbox Live RTA monitoring
#
# Linux note: the plugin is a .so loaded at runtime by OBS.  OBS does NOT
# bundle libwebsockets.so, so we must link libwebsockets statically to avoid a
# hard runtime dependency.  The distro's libwebsockets-dev ships a .a that was
# compiled WITHOUT -fPIC and therefore cannot be embedded in a shared object.
# setup_ubuntu works around this by building libwebsockets from source with
# -DCMAKE_POSITION_INDEPENDENT_CODE=ON and installing it to /usr/local, which
# CMake searches before the system prefix.  The result is a PIC static archive
# that links cleanly into achievements-tracker.so.
#
# LIBWEBSOCKETS_LINKED tracks whether we actually linked the library AND defined
# HAVE_LIBWEBSOCKETS.  It is kept separate from the "package was found" flag so
# that a successful find_package() that doesn't produce a usable target doesn't
# suppress the pkg-config fallback.
set(LIBWEBSOCKETS_LINKED FALSE)

# Method 1: Try CMake's find_package
# On Linux this will pick up the LibwebsocketsConfig.cmake installed to
# /usr/local by the from-source build in setup_ubuntu.
find_package(Libwebsockets QUIET)
if(Libwebsockets_FOUND OR LIBWEBSOCKETS_FOUND)
  # Probe all known target names produced by various libwebsockets CMake configs.
  # The from-source / vcpkg builds create Libwebsockets::websockets (namespaced),
  # older configs may use websockets or websockets_shared.
  set(_lws_target "")
  foreach(_candidate IN ITEMS Libwebsockets::websockets websockets_shared websockets)
    if(TARGET ${_candidate})
      set(_lws_target ${_candidate})
      break()
    endif()
  endforeach()

  if(_lws_target)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${_lws_target})
    # Static libwebsockets on Windows requires Winsock; link it explicitly since
    # the imported target does not always propagate it as a dependency.
    if(WIN32)
      target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32 dbghelp)
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_LIBWEBSOCKETS)
    set(LIBWEBSOCKETS_LINKED TRUE)
    message(STATUS "Found libwebsockets via CMake package (target: ${_lws_target})")
  endif()
endif()

# Method 2: Try pkg-config when CMake method didn't produce a usable target
if(NOT LIBWEBSOCKETS_LINKED)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBWEBSOCKETS QUIET libwebsockets)
    if(LIBWEBSOCKETS_FOUND)
      target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS})
      target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${LIBWEBSOCKETS_LIBRARIES})
      target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_LIBWEBSOCKETS)
      set(LIBWEBSOCKETS_LINKED TRUE)
      message(STATUS "Found libwebsockets via pkg-config")
    endif()
  endif()
endif()

# Method 3: Try manual search in common locations (Homebrew, etc.) — macOS only
if(NOT LIBWEBSOCKETS_LINKED AND APPLE)
  set(HOMEBREW_PREFIXES "/opt/homebrew" "/usr/local")
  foreach(prefix ${HOMEBREW_PREFIXES})
    if(EXISTS "${prefix}/include/libwebsockets.h")
      if(EXISTS "${prefix}/lib/libwebsockets.dylib" OR EXISTS "${prefix}/lib/libwebsockets.a")
        target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${prefix}/include")
        target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE "${prefix}/lib")
        target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE websockets)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAVE_LIBWEBSOCKETS)
        set(LIBWEBSOCKETS_LINKED TRUE)
        message(STATUS "Found libwebsockets in ${prefix}")
        break()
      endif()
    endif()
  endforeach()
endif()

if(NOT LIBWEBSOCKETS_LINKED)
  message(WARNING "libwebsockets not found. Xbox Live RTA monitoring will be disabled.")
  message(WARNING "To enable RTA monitoring, install libwebsockets:")
  message(WARNING "  macOS:   brew install libwebsockets")
  message(
    WARNING
    "  Linux:   built from source with -fPIC by setup_ubuntu (see .github/scripts/utils.zsh/setup_ubuntu)"
  )
  message(WARNING "  Windows: vcpkg install libwebsockets:x64-windows-static (or arm64-windows-static)")
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
  if(WIN32)
    # Static OpenSSL requires ws2_32 (Winsock) and crypt32 (Windows certificate
    # store) which the shared/system build pulls in automatically but the static
    # vcpkg build does not propagate through its imported target.
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ws2_32 crypt32)
  endif()
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
  # Local macOS/dev builds bootstrap OBS into `.deps`, but some IDE / custom
  # configure flows do not preserve the derived prefix path early enough for a
  # plain `find_package(obs-frontend-api)` call to discover it reliably.
  set(
    _obs_frontend_api_config_hints
    "${CMAKE_CURRENT_SOURCE_DIR}/.deps"
    "${CMAKE_CURRENT_SOURCE_DIR}/.deps/lib/cmake/obs-frontend-api"
  )
  file(GLOB _obs_frontend_api_obs_build_dirs "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-*/build_*")
  foreach(_obs_build_dir IN LISTS _obs_frontend_api_obs_build_dirs)
    list(APPEND _obs_frontend_api_config_hints "${_obs_build_dir}/frontend/api" "${_obs_build_dir}/libobs")
  endforeach()

  find_package(
    obs-frontend-api
    CONFIG
    QUIET
    HINTS ${_obs_frontend_api_config_hints}
    PATH_SUFFIXES lib/cmake/obs-frontend-api
  )

  if(NOT TARGET OBS::obs-frontend-api)
    set(_obs_frontend_api_include_hints "${CMAKE_CURRENT_SOURCE_DIR}/.deps/include")
    file(GLOB _obs_frontend_api_source_hints "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-*/frontend/api")
    list(APPEND _obs_frontend_api_include_hints ${_obs_frontend_api_source_hints})

    find_path(
      OBS_FRONTEND_API_INCLUDE_DIR
      NAMES obs-frontend-api.h
      HINTS ${_obs_frontend_api_include_hints}
      PATH_SUFFIXES obs
    )
    find_library(OBS_FRONTEND_API_LIBRARY NAMES obs-frontend-api HINTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/lib")

    if(OBS_FRONTEND_API_INCLUDE_DIR AND OBS_FRONTEND_API_LIBRARY)
      add_library(OBS::obs-frontend-api SHARED IMPORTED)
      set_target_properties(
        OBS::obs-frontend-api
        PROPERTIES
          IMPORTED_LOCATION "${OBS_FRONTEND_API_LIBRARY}"
          INTERFACE_INCLUDE_DIRECTORIES "${OBS_FRONTEND_API_INCLUDE_DIR}"
          INTERFACE_LINK_LIBRARIES "OBS::libobs"
      )
      message(STATUS "Using fallback obs-frontend-api import from ${OBS_FRONTEND_API_LIBRARY}")
    else()
      message(
        FATAL_ERROR
        "Could not find obs-frontend-api. Expected either a CMake package under ${CMAKE_CURRENT_SOURCE_DIR}/.deps or an installed library/header pair.\n"
        "Checked config hints: ${_obs_frontend_api_config_hints}\n"
        "Checked include hints: ${_obs_frontend_api_include_hints}\n"
        "Looked for library under: ${CMAKE_CURRENT_SOURCE_DIR}/.deps/lib"
      )
    endif()
  endif()

  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

set(_qt6_package_hints)
set(_qt6_local_prefix_globs "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-qt6-*")

if(WIN32 AND CMAKE_VS_PLATFORM_NAME)
  string(TOLOWER "${CMAKE_VS_PLATFORM_NAME}" _qt6_target_arch)
  if(_qt6_target_arch STREQUAL "amd64")
    set(_qt6_target_arch "x64")
  elseif(_qt6_target_arch STREQUAL "win32")
    set(_qt6_target_arch "x86")
  endif()

  set(_qt6_local_prefix_globs "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-qt6-*-${_qt6_target_arch}")
endif()

file(GLOB _qt6_local_prefix_hints LIST_DIRECTORIES TRUE ${_qt6_local_prefix_globs})
if(NOT _qt6_local_prefix_hints)
  file(GLOB _qt6_local_prefix_hints LIST_DIRECTORIES TRUE "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-qt6-*")
endif()

list(SORT _qt6_local_prefix_hints)
foreach(_qt6_prefix IN LISTS _qt6_local_prefix_hints)
  list(APPEND _qt6_package_hints "${_qt6_prefix}" "${_qt6_prefix}/lib/cmake/Qt6")
endforeach()

if(WIN32 AND COMMAND _handle_qt_cross_compile AND _qt6_local_prefix_hints)
  list(GET _qt6_local_prefix_hints 0 _qt6_primary_prefix)
  _handle_qt_cross_compile(DIRECTORY "${_qt6_primary_prefix}")
endif()

find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets Core HINTS ${_qt6_package_hints} PATH_SUFFIXES lib/cmake/Qt6)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)

# ----------------------------------------------------------------------------
# macOS: Remove legacy AGL.framework from Qt6's WrapOpenGL::WrapOpenGL target.
#
# Qt6's FindWrapOpenGL.cmake unconditionally adds -framework AGL as a
# transitive dependency of Qt6::Gui.  AGL (Apple OpenGL) was removed from the
# macOS 15+ SDK, so the linker fails with "framework 'AGL' not found" when
# building against a modern SDK.  Strip it out after Qt is found.
# ----------------------------------------------------------------------------
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
  get_target_property(_wrap_opengl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
  if(_wrap_opengl_libs)
    list(REMOVE_ITEM _wrap_opengl_libs "-framework AGL")
    # Also handle the case where it was found as an absolute path / find_library result
    list(FILTER _wrap_opengl_libs EXCLUDE REGEX "AGL\\.framework")
    set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_wrap_opengl_libs}")
    message(STATUS "Removed AGL.framework from WrapOpenGL::WrapOpenGL (not present in macOS 15+ SDK)")
  endif()
endif()
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
    -Wno-comma>
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
    -Wno-comma>
)
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)

# FreeType for text rendering
#
# IMPORTANT (macOS universal): do NOT link against Homebrew's FreeType.
# On arm64 runners, Homebrew's freetype bottle is arm64-only, which breaks
# universal (arm64+x86_64) linking.
#
# For universal builds, ensure FreeType is available in CMAKE_PREFIX_PATH
# as a universal binary (e.g., from obs-deps or a custom universal build).
if(APPLE)
  # Ignore typical Homebrew prefixes to prevent finding arm64-only FreeType
  list(APPEND CMAKE_IGNORE_PREFIX_PATH "/opt/homebrew" "/usr/local")
endif()

# Prefer CMake's built-in FindFreetype (works well on Windows and macOS/Linux).
find_package(Freetype QUIET)

if(Freetype_FOUND)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Freetype::Freetype)

  # If FreeType resolves to Homebrew on macOS, fail early ONLY for universal builds.
  if(APPLE)
    get_target_property(FREETYPE_LIB Freetype::Freetype IMPORTED_LOCATION)
    if(NOT FREETYPE_LIB)
      # Try alternative properties
      get_target_property(FREETYPE_LIB Freetype::Freetype LOCATION)
    endif()

    list(LENGTH CMAKE_OSX_ARCHITECTURES _ft_arch_count)
    if(_ft_arch_count GREATER 1)
      # Universal build - require non-Homebrew FreeType
      if(FREETYPE_LIB MATCHES "(/opt/homebrew|/usr/local)")
        message(
          FATAL_ERROR
          "Found Homebrew's FreeType at ${FREETYPE_LIB} which is often single-arch.\n"
          "This breaks universal (arm64+x86_64) builds.\n"
          "Install/use a universal FreeType (add to obs-deps or build manually).\n"
          "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}\n"
          "For local testing, use the 'macos-dev' preset which builds for native arch only."
        )
      endif()
    else()
      # Single-arch build - Homebrew FreeType is fine
      if(FREETYPE_LIB MATCHES "(/opt/homebrew|/usr/local)")
        message(STATUS "Using Homebrew FreeType for single-arch dev build: ${FREETYPE_LIB}")
      endif()
    endif()
  endif()
else()
  # Fallback to pkg-config (typically macOS/Linux)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(FREETYPE QUIET IMPORTED_TARGET freetype2)
    if(FREETYPE_FOUND)
      target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE PkgConfig::FREETYPE)
      target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${FREETYPE_INCLUDE_DIRS})
      target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE ${FREETYPE_CFLAGS_OTHER})

      # Check if FreeType is from Homebrew for universal builds
      if(APPLE)
        list(LENGTH CMAKE_OSX_ARCHITECTURES _ft_arch_count2)
        if(_ft_arch_count2 GREATER 1)
          foreach(lib ${FREETYPE_LIBRARIES})
            if(lib MATCHES "(/opt/homebrew|/usr/local)")
              message(
                FATAL_ERROR
                "Found Homebrew's FreeType at ${lib} which is arm64-only.\n"
                "This breaks universal (arm64+x86_64) builds.\n"
                "Install/use a universal FreeType (add to obs-deps or build manually).\n"
                "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}\n"
                "For local testing, use the 'macos-dev' preset which builds for native arch only."
              )
            endif()
          endforeach()
        endif()
      endif()
    endif()
  endif()
endif()

# If you \*require\* FreeType whenever `src/drawing/text.c` is compiled, fail hard.
if(NOT Freetype_FOUND AND NOT FREETYPE_FOUND)
  message(
    FATAL_ERROR
    "FreeType not found (needed for `src/drawing/text.c`). Install freetype and/or set CMAKE_PREFIX_PATH."
  )
endif()

# FreeType requires zlib for gzip compression support
find_package(ZLIB REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ZLIB::ZLIB)

# Logging helpers (obs_log wrapper + plugin name/version)
if(TARGET diagnostics-log)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE diagnostics-log)
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/main.c
    src/sources/xbox/gamerpic.c
    src/sources/xbox/game_cover.c
    src/sources/xbox/gamerscore.c
    src/sources/xbox/gamertag.c
    src/sources/xbox/achievement_name.c
    src/sources/xbox/achievement_description.c
    src/sources/xbox/achievement_icon.c
    src/sources/xbox/achievements_count.c
    src/sources/common/text_source.c
    src/sources/common/image_source.c
    src/sources/common/achievement_cycle.c
    src/crypto/crypto.c
    src/drawing/color.c
    src/drawing/image.c
    src/net/browser/browser.c
    src/net/http/http.c
    src/net/json/json.c
    src/integrations/xbox/oauth/util.c
    src/integrations/xbox/oauth/xbox-live.c
    src/integrations/xbox/account_manager.c
    src/integrations/xbox/xbox_session.c
    src/integrations/xbox/xbox_client.c
    src/integrations/xbox/xbox_monitor.c
    src/integrations/retro-achievements/retro_achievements_monitor.c
    src/ui/xbox_account_config.cpp
    src/io/state.c
    src/io/cache.c
    src/encoding/base64.c
    src/util/uuid.c
    src/text/convert.c
    src/text/parsers.c
    src/time/time.c
    src/common/achievement.c
    src/common/achievement_progress.c
    src/common/device.c
    src/common/game.c
    src/common/gamerscore.c
    src/common/identity.c
    src/common/token.c
    src/common/unlocked_achievement.c
    src/common/xbox_identity.c
    src/common/xbox_session.c
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
  # Helper function to link common dependencies to test targets
  # ------------------------------
  function(target_link_test_deps target_name)
    # Link vendored deps
    target_link_libraries(${target_name} PRIVATE cjson)
    if(UNIX AND NOT APPLE)
      target_link_libraries(${target_name} PRIVATE m)
    endif()

    # OpenSSL
    if(TARGET OpenSSL::Crypto)
      target_link_libraries(${target_name} PRIVATE OpenSSL::Crypto)
      if(TARGET OpenSSL::SSL)
        target_link_libraries(${target_name} PRIVATE OpenSSL::SSL)
      endif()
      # Ensure headers are available even if the imported targets don't propagate include dirs.
      if(OPENSSL_INCLUDE_DIR)
        target_include_directories(${target_name} PRIVATE ${OPENSSL_INCLUDE_DIR})
      endif()
      if(WIN32)
        target_link_libraries(${target_name} PRIVATE ws2_32 crypt32 dbghelp)
      endif()
    else()
      target_include_directories(${target_name} PRIVATE ${OPENSSL_INCLUDE_DIR})
      target_link_libraries(${target_name} PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
      if(OPENSSL_SSL_LIBRARY)
        target_link_libraries(${target_name} PRIVATE ${OPENSSL_SSL_LIBRARY})
      endif()
    endif()
  endfunction()

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

  add_test(NAME test_encoder COMMAND test_encoder)

  if(ENABLE_COVERAGE)
    enable_coverage(test_encoder)
  endif()

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

  target_link_test_deps(test_encoder)

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

  add_test(NAME test_crypto COMMAND test_crypto)

  if(ENABLE_COVERAGE)
    enable_coverage(test_crypto)
  endif()

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

  target_link_test_deps(test_crypto)

  # ------------------------------
  # test_convert
  # ------------------------------
  add_executable(
    test_convert
    test/test_convert.c
    ${unity_SOURCE_DIR}/src/unity.c
    src/text/convert.c
    test/stubs/bmem_stub.c
  )

  add_test(NAME test_convert COMMAND test_convert)

  if(ENABLE_COVERAGE)
    enable_coverage(test_convert)
  endif()

  target_include_directories(
    test_convert
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/test/stubs
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${unity_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/test
  )

  target_compile_definitions(test_convert PRIVATE UNITY_INCLUDE_CONFIG_H)

  target_link_test_deps(test_convert)

  # ------------------------------
  # test_parsers
  # ------------------------------
  add_executable(
    test_parsers
    test/test_parsers.c
    ${unity_SOURCE_DIR}/src/unity.c
    src/text/convert.c
    src/text/parsers.c
    test/stubs/bmem_stub.c
  )

  add_test(NAME test_parsers COMMAND test_parsers)

  if(ENABLE_COVERAGE)
    enable_coverage(test_parsers)
  endif()

  target_include_directories(
    test_parsers
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/test/stubs
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${unity_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/test
  )

  target_compile_definitions(test_parsers PRIVATE UNITY_INCLUDE_CONFIG_H)

  target_link_test_deps(test_parsers)

  # ------------------------------
  # test_xbox_session
  # ------------------------------
  add_executable(
    test_xbox_session
    test/test_xbox_session.c
    ${unity_SOURCE_DIR}/src/unity.c
    src/integrations/xbox/xbox_session.c
    src/common/achievement.c
    src/common/achievement_progress.c
    src/common/game.c
    src/common/gamerscore.c
    src/common/token.c
    src/common/unlocked_achievement.c
    src/common/xbox_session.c
    test/stubs/bmem_stub.c
    test/stubs/xbox/xbox_client_stub.c
    test/stubs/time/time_stub.c
    test/stubs/io/cache_stub.c
  )

  add_test(NAME test_xbox_session COMMAND test_xbox_session)

  if(ENABLE_COVERAGE)
    enable_coverage(test_xbox_session)
  endif()

  target_include_directories(
    test_xbox_session
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/test/stubs
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${unity_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/test
  )

  target_compile_definitions(test_xbox_session PRIVATE UNITY_INCLUDE_CONFIG_H)

  target_link_test_deps(test_xbox_session)

  # ------------------------------
  # test_types
  # ------------------------------
  add_executable(
    test_types
    test/test_types.c
    ${unity_SOURCE_DIR}/src/unity.c
    src/integrations/xbox/xbox_session.c
    src/common/achievement.c
    src/common/achievement_progress.c
    src/common/game.c
    src/common/gamerscore.c
    src/common/token.c
    src/common/unlocked_achievement.c
    src/common/xbox_session.c
    test/stubs/bmem_stub.c
    test/stubs/xbox/xbox_client_stub.c
    test/stubs/time/time_stub.c
    test/stubs/io/cache_stub.c
  )

  add_test(NAME test_types COMMAND test_types)

  if(ENABLE_COVERAGE)
    enable_coverage(test_types)
  endif()

  target_include_directories(
    test_types
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/test/stubs
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${unity_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/test
  )

  target_compile_definitions(test_types PRIVATE UNITY_INCLUDE_CONFIG_H)

  target_link_test_deps(test_types)

  # ------------------------------
  # Coverage target (must be after all test targets are defined)
  # ------------------------------
  if(ENABLE_COVERAGE)
    add_coverage_target(test_encoder test_crypto test_convert test_parsers test_xbox_session test_types)
  endif()
endif()
