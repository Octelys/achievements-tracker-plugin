include(FetchContent)

# Keep cURL pinned for reproducibility.
# Build as a static library so the correct version is embedded in the plugin binary.
# Must be included AFTER OpenSSL has been resolved by the parent project.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_TESTS ON CACHE BOOL "" FORCE)

# On macOS, use Apple's built-in Secure Transport (no OpenSSL dependency needed for cURL).
# On other platforms, use OpenSSL and forward the paths already resolved by the parent project.
if(APPLE)
  set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
  set(USE_APPLE_SECTRUST ON CACHE BOOL "" FORCE)
  # Point cURL at Homebrew's OpenSSL so it can find both libssl and libcrypto.
  # Use brew to determine the actual prefix, falling back to known paths.
  if(NOT DEFINED HOMEBREW_OPENSSL_PREFIX)
    find_program(_brew_cmd brew)
    if(_brew_cmd)
      execute_process(
        COMMAND "${_brew_cmd}" --prefix openssl@3
        OUTPUT_VARIABLE HOMEBREW_OPENSSL_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
      )
    endif()
    if(NOT HOMEBREW_OPENSSL_PREFIX OR NOT EXISTS "${HOMEBREW_OPENSSL_PREFIX}")
      # Fallback to known Apple Silicon / Intel Homebrew paths
      foreach(_p "/opt/homebrew/opt/openssl@3" "/usr/local/opt/openssl@3")
        if(EXISTS "${_p}/include/openssl/ssl.h")
          set(HOMEBREW_OPENSSL_PREFIX "${_p}")
          break()
        endif()
      endforeach()
    endif()
  endif()
  if(HOMEBREW_OPENSSL_PREFIX AND EXISTS "${HOMEBREW_OPENSSL_PREFIX}")
    message(STATUS "FetchCURL: using OpenSSL from ${HOMEBREW_OPENSSL_PREFIX}")
    set(OPENSSL_ROOT_DIR "${HOMEBREW_OPENSSL_PREFIX}" CACHE PATH "" FORCE)
    set(OPENSSL_INCLUDE_DIR "${HOMEBREW_OPENSSL_PREFIX}/include" CACHE PATH "" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY "${HOMEBREW_OPENSSL_PREFIX}/lib/libcrypto.dylib" CACHE FILEPATH "" FORCE)
    set(OPENSSL_SSL_LIBRARY "${HOMEBREW_OPENSSL_PREFIX}/lib/libssl.dylib" CACHE FILEPATH "" FORCE)
  endif()
else()
  set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
  # Forward OpenSSL already resolved by the parent project
  if(OPENSSL_ROOT_DIR)
    set(OPENSSL_ROOT_DIR "${OPENSSL_ROOT_DIR}" CACHE PATH "" FORCE)
  endif()
  if(OPENSSL_INCLUDE_DIR)
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_INCLUDE_DIR}" CACHE PATH "" FORCE)
  endif()
  if(OPENSSL_CRYPTO_LIBRARY)
    set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_CRYPTO_LIBRARY}" CACHE FILEPATH "" FORCE)
  endif()
  if(OPENSSL_SSL_LIBRARY)
    set(OPENSSL_SSL_LIBRARY "${OPENSSL_SSL_LIBRARY}" CACHE FILEPATH "" FORCE)
  endif()
endif()

# Disable optional deps that may not be present on all machines
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)

# Disable protocols/features we don't need to keep the build lean
set(CURL_DISABLE_DICT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FILE ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_GOPHER ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_IMAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_MQTT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_POP3 ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_RTSP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMB ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TELNET ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TFTP ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  curl
  URL https://curl.se/download/curl-8.18.0.tar.gz
  URL_HASH SHA256=e9274a5f8ab5271c0e0e6762d2fce194d5f98acc568e4ce816845b2dcc0cf88f
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(curl)
