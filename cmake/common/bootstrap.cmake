# Plugin bootstrap module

include_guard(GLOBAL)

# Map fallback configurations for optimized build configurations
# gersemi: off
set(
  CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO
    RelWithDebInfo
    Release
    MinSizeRel
    None
    ""
)
set(
  CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL
    MinSizeRel
    Release
    RelWithDebInfo
    None
    ""
)
set(
  CMAKE_MAP_IMPORTED_CONFIG_RELEASE
    Release
    RelWithDebInfo
    MinSizeRel
    None
    ""
)
# gersemi: on

# Prohibit in-source builds
if("${CMAKE_CURRENT_BINARY_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
  message(
    FATAL_ERROR
    "In-source builds are not supported. "
    "Specify a build directory via 'cmake -S <SOURCE DIRECTORY> -B <BUILD_DIRECTORY>' instead."
  )
  file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/CMakeCache.txt" "${CMAKE_CURRENT_SOURCE_DIR}/CMakeFiles")
endif()

# Add common module directories to default search path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/common")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" buildspec)

string(JSON _name GET ${buildspec} name)
string(JSON _website GET ${buildspec} website)
string(JSON _author GET ${buildspec} author)
string(JSON _email GET ${buildspec} email)
string(JSON _bundleId GET ${buildspec} platformConfig macos bundleId)

# displayName is optional — fall back to name if absent
string(JSON _displayName ERROR_VARIABLE _displayName_error GET ${buildspec} displayName)
if(_displayName_error OR NOT _displayName)
  set(_displayName ${_name})
endif()

set(PLUGIN_AUTHOR ${_author})
set(PLUGIN_WEBSITE ${_website})
set(PLUGIN_EMAIL ${_email})
set(PLUGIN_DISPLAY_NAME ${_displayName})
set(MACOS_BUNDLEID ${_bundleId})

# Resolve build number first (needed for version computation)
include(buildnumber)

# Compute version as YY.MMDD.BuildNumber from the current date
string(TIMESTAMP _year "%Y")
string(TIMESTAMP _month "%m")
string(TIMESTAMP _day "%d")
math(EXPR PLUGIN_VERSION_MAJOR "${_year} % 100")
set(PLUGIN_VERSION_MINOR "${_month}${_day}")
set(PLUGIN_VERSION_PATCH "${PLUGIN_BUILD_NUMBER}")
# Strip leading zeros from minor (MMDD) so CMake treats it as a plain integer
math(EXPR PLUGIN_VERSION_MINOR "${PLUGIN_VERSION_MINOR} + 0")
set(_computed_version "${PLUGIN_VERSION_MAJOR}.${PLUGIN_VERSION_MINOR}.${PLUGIN_VERSION_PATCH}")

# Allow the CI to inject a tag-based version (e.g. "1.4.0" from a Git tag).
# If PLUGIN_VERSION_OVERRIDE is set and non-empty, use it; otherwise fall back
# to the date-based computed version.
if(DEFINED PLUGIN_VERSION_OVERRIDE AND NOT "${PLUGIN_VERSION_OVERRIDE}" STREQUAL "")
  set(PLUGIN_VERSION "${PLUGIN_VERSION_OVERRIDE}")
  message(STATUS "Plugin version: ${PLUGIN_VERSION} (from tag override)")
else()
  set(PLUGIN_VERSION "${_computed_version}")
  message(STATUS "Plugin version: ${PLUGIN_VERSION} (computed from date)")
endif()

unset(_computed_version)
unset(_year)
unset(_month)
unset(_day)

include(osconfig)

# Allow selection of common build types via UI
if(NOT CMAKE_GENERATOR MATCHES "(Xcode|Visual Studio .+)")
  if(NOT CMAKE_BUILD_TYPE)
    set(
      CMAKE_BUILD_TYPE
      "RelWithDebInfo"
      CACHE STRING
      "OBS build type [Release, RelWithDebInfo, Debug, MinSizeRel]"
      FORCE
    )
    set_property(
      CACHE CMAKE_BUILD_TYPE
      PROPERTY STRINGS Release RelWithDebInfo Debug MinSizeRel
    )
  endif()
endif()

# Disable exports automatically going into the CMake package registry
set(CMAKE_EXPORT_PACKAGE_REGISTRY FALSE)
# Enable default inclusion of targets' source and binary directory
set(CMAKE_INCLUDE_CURRENT_DIR TRUE)
