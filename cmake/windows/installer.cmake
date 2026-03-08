# CMake Windows CPack/NSIS installer configuration

include_guard(GLOBAL)

# Resolve architecture-specific values
if(CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64")
  set(INSTALLER_ARCH "ARM64")
  set(PLUGIN_BIN_SUBDIR "64bit") # OBS uses 64bit for all Windows archs
else()
  set(INSTALLER_ARCH "x64")
  set(PLUGIN_BIN_SUBDIR "64bit")
endif()

# Install NSIS on CI if not already present
find_program(NSIS_MAKENSIS makensis HINTS
  "C:/Program Files (x86)/NSIS"
  "C:/Program Files/NSIS"
)

if(NOT NSIS_MAKENSIS)
  message(STATUS "makensis not found — installer target will not be available")
  return()
endif()

message(STATUS "Found makensis: ${NSIS_MAKENSIS}")

# Configure the .nsi script from the template
set(CPACK_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
set(CPACK_PACKAGE_FILE_NAME
  "${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}-windows-${INSTALLER_ARCH}"
)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows/installer.nsi.in"
  "${CMAKE_CURRENT_BINARY_DIR}/installer-${INSTALLER_ARCH}.nsi"
  @ONLY
)

# Custom target: cmake --build --target package-installer
set(_nsis_icon_flag "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows/resources/installer.ico")
  set(_nsis_icon_flag "/DHAVE_INSTALLER_ICO")
endif()

add_custom_target(package-installer
  COMMAND "${NSIS_MAKENSIS}"
    /V2
    ${_nsis_icon_flag}
    "${CMAKE_CURRENT_BINARY_DIR}/installer-${INSTALLER_ARCH}.nsi"
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  COMMENT "Building NSIS installer for ${INSTALLER_ARCH}..."
  VERBATIM
)

# Ensure the plugin is installed before building the installer
add_dependencies(package-installer ${CMAKE_PROJECT_NAME})

