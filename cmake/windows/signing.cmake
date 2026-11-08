# CMake Windows codesigning helpers

include_guard(GLOBAL)

option(WINDOWS_CODESIGN "Enable Authenticode signing for Windows binaries and installers" OFF)

set(
  WINDOWS_SIGNTOOL_PATH
  ""
  CACHE FILEPATH
  "Optional full path to signtool.exe. When empty, the Windows SDK installation is searched."
)

function(windows_get_sign_file_script out_var)
  set(${out_var} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows/sign-file.cmake" PARENT_SCOPE)
endfunction()

