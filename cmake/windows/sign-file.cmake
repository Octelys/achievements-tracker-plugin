# Sign a Windows binary or installer with signtool.
#
# Expected variables:
#   SIGN_FILE_PATH - absolute path to the file to sign
#
# Optional environment variables:
#   WINDOWS_SIGNTOOL_PATH
#   WINDOWS_SIGN_CERT_FILE
#   WINDOWS_SIGN_CERT_PASSWORD
#   WINDOWS_SIGN_CERT_SHA1
#   WINDOWS_SIGN_CERT_STORE
#   WINDOWS_SIGN_TIMESTAMP_URL
#   WINDOWS_SIGN_FILE_DIGEST
#   WINDOWS_SIGN_TIMESTAMP_DIGEST
#   WINDOWS_SIGN_DESCRIPTION
#   WINDOWS_SIGN_DESCRIPTION_URL

if(NOT DEFINED SIGN_FILE_PATH OR SIGN_FILE_PATH STREQUAL "")
  message(FATAL_ERROR "SIGN_FILE_PATH must be provided to sign-file.cmake")
endif()

if(NOT EXISTS "${SIGN_FILE_PATH}")
  message(FATAL_ERROR "Cannot sign missing file: ${SIGN_FILE_PATH}")
endif()

set(_signtool "$ENV{WINDOWS_SIGNTOOL_PATH}")

if(_signtool STREQUAL "")
  set(_signtool_hints "")
  foreach(_sdk_root IN ITEMS "C:/Program Files (x86)/Windows Kits/10/bin" "$ENV{ProgramFiles}/Windows Kits/10/bin")
    if(EXISTS "${_sdk_root}")
      file(GLOB _sdk_versions LIST_DIRECTORIES TRUE "${_sdk_root}/*")
      list(SORT _sdk_versions COMPARE NATURAL ORDER DESCENDING)
      foreach(_sdk_version IN LISTS _sdk_versions)
        if(IS_DIRECTORY "${_sdk_version}")
          list(APPEND _signtool_hints "${_sdk_version}/x64" "${_sdk_version}/x86")
        endif()
      endforeach()
    endif()
  endforeach()

  find_program(_signtool NAMES signtool.exe signtool HINTS ${_signtool_hints})
endif()

if(NOT _signtool)
  message(FATAL_ERROR "signtool.exe was not found. Set WINDOWS_SIGNTOOL_PATH or install the Windows SDK.")
endif()

set(_timestamp_url "$ENV{WINDOWS_SIGN_TIMESTAMP_URL}")
if(_timestamp_url STREQUAL "")
  set(_timestamp_url "http://timestamp.digicert.com")
endif()

set(_file_digest "$ENV{WINDOWS_SIGN_FILE_DIGEST}")
if(_file_digest STREQUAL "")
  set(_file_digest "SHA256")
endif()

set(_timestamp_digest "$ENV{WINDOWS_SIGN_TIMESTAMP_DIGEST}")
if(_timestamp_digest STREQUAL "")
  set(_timestamp_digest "SHA256")
endif()

set(_cert_file "$ENV{WINDOWS_SIGN_CERT_FILE}")
set(_cert_password "$ENV{WINDOWS_SIGN_CERT_PASSWORD}")
set(_cert_sha1 "$ENV{WINDOWS_SIGN_CERT_SHA1}")
set(_cert_store "$ENV{WINDOWS_SIGN_CERT_STORE}")
set(_description "$ENV{WINDOWS_SIGN_DESCRIPTION}")
set(_description_url "$ENV{WINDOWS_SIGN_DESCRIPTION_URL}")

set(_sign_args sign /fd ${_file_digest} /td ${_timestamp_digest} /tr ${_timestamp_url})

if(NOT _description STREQUAL "")
  list(APPEND _sign_args /d ${_description})
endif()

if(NOT _description_url STREQUAL "")
  list(APPEND _sign_args /du ${_description_url})
endif()

if(NOT _cert_file STREQUAL "")
  if(NOT EXISTS "${_cert_file}")
    message(FATAL_ERROR "WINDOWS_SIGN_CERT_FILE does not exist: ${_cert_file}")
  endif()

  list(APPEND _sign_args /f ${_cert_file})
  if(NOT _cert_password STREQUAL "")
    list(APPEND _sign_args /p ${_cert_password})
  endif()
elseif(NOT _cert_sha1 STREQUAL "")
  list(APPEND _sign_args /sha1 ${_cert_sha1})
  if(NOT _cert_store STREQUAL "")
    list(APPEND _sign_args /s ${_cert_store})
  endif()
else()
  message(
    FATAL_ERROR
    "Windows codesigning is enabled but no certificate was configured. Set WINDOWS_SIGN_CERT_FILE (+ optional WINDOWS_SIGN_CERT_PASSWORD) or WINDOWS_SIGN_CERT_SHA1."
  )
endif()

list(APPEND _sign_args ${SIGN_FILE_PATH})

message(STATUS "Signing Windows artifact: ${SIGN_FILE_PATH}")
execute_process(
  COMMAND "${_signtool}" ${_sign_args}
  RESULT_VARIABLE _sign_result
  COMMAND_ECHO STDOUT
)

if(NOT _sign_result EQUAL 0)
  message(FATAL_ERROR "signtool failed for ${SIGN_FILE_PATH} with exit code ${_sign_result}")
endif()

execute_process(
  COMMAND "${_signtool}" verify /pa /v ${SIGN_FILE_PATH}
  RESULT_VARIABLE _verify_result
  COMMAND_ECHO STDOUT
)

if(NOT _verify_result EQUAL 0)
  message(FATAL_ERROR "signtool verification failed for ${SIGN_FILE_PATH} with exit code ${_verify_result}")
endif()

