# CMake Windows CPack/NSIS installer configuration

include_guard(GLOBAL)

# Resolve architecture-specific values
if(CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64")
  set(INSTALLER_ARCH "ARM64")
  set(PLUGIN_BIN_SUBDIR "64bit")
else()
  set(INSTALLER_ARCH "x64")
  set(PLUGIN_BIN_SUBDIR "64bit")
endif()

find_program(NSIS_MAKENSIS makensis HINTS "C:/Program Files (x86)/NSIS" "C:/Program Files/NSIS")

if(NOT NSIS_MAKENSIS)
  message(STATUS "makensis not found — installer target will not be available")
  return()
endif()

message(STATUS "Found makensis: ${NSIS_MAKENSIS}")

set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}-windows-${INSTALLER_ARCH}")

set(_nsi_template "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows/installer.nsi.in")
set(_nsi_configured "${CMAKE_CURRENT_BINARY_DIR}/installer-${INSTALLER_ARCH}.nsi")
set(_configure_script "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows/configure-installer.cmake")
set(_installer_output "${CMAKE_CURRENT_BINARY_DIR}/${CPACK_PACKAGE_FILE_NAME}.exe")

set(_nsis_icon_flag "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows/resources/installer.ico")
  set(_nsis_icon_flag "/DHAVE_INSTALLER_ICO")
endif()

if(WINDOWS_CODESIGN)
  windows_get_sign_file_script(_windows_sign_file_script)
  set(_windows_sign_installer_command
    COMMAND
      "${CMAKE_COMMAND}" -DSIGN_FILE_PATH=${_installer_output} -P "${_windows_sign_file_script}"
  )
else()
  set(_windows_sign_installer_command "")
endif()

# Custom target: cmake --build --target package-installer
# Step 1 re-runs configure-installer.cmake at build time with the real
# $<CONFIG> so INSTALL_STAGE_DIR points at the correct staged directory.
add_custom_target(
  package-installer
  COMMAND
    "${CMAKE_COMMAND}" -DCMAKE_PROJECT_NAME=${CMAKE_PROJECT_NAME} -DPLUGIN_DISPLAY_NAME=${PLUGIN_DISPLAY_NAME}
    -DCMAKE_PROJECT_VERSION=${CMAKE_PROJECT_VERSION} -DPLUGIN_AUTHOR=${PLUGIN_AUTHOR} -DINSTALLER_ARCH=${INSTALLER_ARCH}
    -DPLUGIN_BIN_SUBDIR=${PLUGIN_BIN_SUBDIR} -DCPACK_PACKAGE_FILE_NAME=${CPACK_PACKAGE_FILE_NAME}
    -DCMAKE_CURRENT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
    "-DINSTALL_STAGE_DIR=${CMAKE_CURRENT_BINARY_DIR}/../release/$<CONFIG>" -DNSI_TEMPLATE=${_nsi_template}
    -DNSI_OUTPUT=${_nsi_configured} -P ${_configure_script}
  COMMAND "${NSIS_MAKENSIS}" /V2 ${_nsis_icon_flag} "${_nsi_configured}"
  ${_windows_sign_installer_command}
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  COMMENT "Building NSIS installer for ${INSTALLER_ARCH}..."
  VERBATIM
)

add_dependencies(package-installer ${CMAKE_PROJECT_NAME})
