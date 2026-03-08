# configure-installer.cmake
#
# Invoked at BUILD time (cmake -P) by the package-installer custom target.
# All variables are passed in via -D flags so that generator expressions
# (e.g. $<CONFIG>) have already been resolved by MSBuild/Ninja before this
# script runs.
#
# Required -D variables:
#   CMAKE_PROJECT_NAME, PLUGIN_DISPLAY_NAME, CMAKE_PROJECT_VERSION,
#   PLUGIN_AUTHOR, INSTALLER_ARCH, PLUGIN_BIN_SUBDIR,
#   CPACK_PACKAGE_FILE_NAME, CMAKE_CURRENT_SOURCE_DIR,
#   INSTALL_STAGE_DIR, NSI_TEMPLATE, NSI_OUTPUT

# Normalise the stage dir path to use backslashes for NSIS
file(TO_NATIVE_PATH "${INSTALL_STAGE_DIR}" INSTALL_STAGE_DIR)

configure_file("${NSI_TEMPLATE}" "${NSI_OUTPUT}" @ONLY)
message(STATUS "Configured NSIS script: ${NSI_OUTPUT}")
message(STATUS "  Stage dir : ${INSTALL_STAGE_DIR}")

