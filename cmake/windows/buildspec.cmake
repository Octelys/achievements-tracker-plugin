# CMake Windows build dependencies module

include_guard(GLOBAL)

include(buildspec_common)

# _download_windows_qt_host_tools: Ensure the host-side Qt package exists for cross-compiles.
function(_download_windows_qt_host_tools host_arch out_var)
  set(dependencies_dir "${CMAKE_CURRENT_SOURCE_DIR}/.deps")

  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" _buildspec)
  string(JSON _qt6_data GET ${_buildspec} dependencies qt6)
  string(JSON _qt6_version GET ${_qt6_data} version)
  string(JSON _qt6_hash GET ${_qt6_data} hashes windows-${host_arch})
  string(JSON _qt6_base_url GET ${_qt6_data} baseUrl)
  string(JSON _qt6_revision ERROR_VARIABLE _qt6_revision_error GET ${_qt6_data} revision windows-${host_arch})

  set(_qt6_filename "windows-deps-qt6-VERSION-ARCH-REVISION.zip")
  set(_qt6_destination "obs-deps-qt6-VERSION-ARCH")
  string(REPLACE "VERSION" "${_qt6_version}" _qt6_filename "${_qt6_filename}")
  string(REPLACE "VERSION" "${_qt6_version}" _qt6_destination "${_qt6_destination}")
  string(REPLACE "ARCH" "${host_arch}" _qt6_filename "${_qt6_filename}")
  string(REPLACE "ARCH" "${host_arch}" _qt6_destination "${_qt6_destination}")

  if(_qt6_revision AND NOT _qt6_revision_error)
    string(REPLACE "_REVISION" "_v${_qt6_revision}" _qt6_filename "${_qt6_filename}")
    string(REPLACE "-REVISION" "-v${_qt6_revision}" _qt6_filename "${_qt6_filename}")
  else()
    string(REPLACE "_REVISION" "" _qt6_filename "${_qt6_filename}")
    string(REPLACE "-REVISION" "" _qt6_filename "${_qt6_filename}")
  endif()

  set(_qt6_archive_path "${dependencies_dir}/${_qt6_filename}")
  set(_qt6_destination_path "${dependencies_dir}/${_qt6_destination}")
  set(_qt6_hash_stamp "${dependencies_dir}/.dependency_qt6_${host_arch}.sha256")

  if(EXISTS "${_qt6_hash_stamp}")
    file(READ "${_qt6_hash_stamp}" _qt6_recorded_hash)
  endif()

  if(NOT _qt6_recorded_hash STREQUAL "${_qt6_hash}")
    file(REMOVE "${_qt6_archive_path}")
    file(REMOVE_RECURSE "${_qt6_destination_path}")
  endif()

  if(NOT EXISTS "${_qt6_archive_path}")
    set(_qt6_url "${_qt6_base_url}/${_qt6_version}/${_qt6_filename}")
    message(STATUS "Downloading Qt6 host tools package for ${host_arch}: ${_qt6_url}")
    file(DOWNLOAD "${_qt6_url}" "${_qt6_archive_path}" STATUS _qt6_download_status EXPECTED_HASH SHA256=${_qt6_hash})

    list(GET _qt6_download_status 0 _qt6_error_code)
    list(GET _qt6_download_status 1 _qt6_error_message)
    if(_qt6_error_code GREATER 0)
      file(REMOVE "${_qt6_archive_path}")
      message(FATAL_ERROR "Unable to download ${_qt6_url}, failed with error: ${_qt6_error_message}")
    endif()
  endif()

  if(NOT EXISTS "${_qt6_destination_path}")
    file(MAKE_DIRECTORY "${_qt6_destination_path}")
    file(ARCHIVE_EXTRACT INPUT "${_qt6_archive_path}" DESTINATION "${_qt6_destination_path}")
  endif()

  file(WRITE "${_qt6_hash_stamp}" "${_qt6_hash}")

  set(${out_var} "${_qt6_destination_path}" PARENT_SCOPE)
endfunction()

# _handle_qt_cross_compile: Detect the required host-side Qt tools package for Windows cross-compiles.
function(_handle_qt_cross_compile)
  set(options "")
  set(oneValueArgs DIRECTORY)
  set(multiValueArgs "")
  cmake_parse_arguments(PARSE_ARGV 0 _HQCC "${options}" "${oneValueArgs}" "${multiValueArgs}")

  if(NOT ENABLE_QT OR NOT IS_DIRECTORY "${_HQCC_DIRECTORY}")
    return()
  endif()

  set(_qt_qconfig_pri "${_HQCC_DIRECTORY}/mkspecs/qconfig.pri")
  if(NOT EXISTS "${_qt_qconfig_pri}")
    message(FATAL_ERROR "Unable to detect Qt build/target architecture because '${_qt_qconfig_pri}' does not exist")
  endif()

  file(READ "${_qt_qconfig_pri}" _qt_arch_config)
  string(REPLACE "\r\n" "\n" _qt_arch_config "${_qt_arch_config}")

  set(_qt_build_arch "")
  set(_qt_target_arch "")
  set(_qt_cross_compiled FALSE)
  set(_qt_config_has_buildabi FALSE)

  string(REGEX MATCH ".+QT_TARGET_BUILDABI = (.+)\n.+" _qt_config_has_buildabi "${_qt_arch_config}")
  if(_qt_config_has_buildabi)
    string(
      REGEX REPLACE
      "host_build {\n[ \t]+QT_ARCH = (x86_64|arm64)\n.+[ \t]+QT_TARGET_ARCH = (x86_64|arm64)\n.+}.+"
      "\\1;\\2"
      _qt_host_build_tuple
      "${_qt_arch_config}"
    )
    list(GET _qt_host_build_tuple 0 _qt_build_arch)
    list(GET _qt_host_build_tuple 1 _qt_target_arch)
    set(_qt_cross_compiled TRUE)
  else()
    string(REGEX REPLACE ".*QT_ARCH = (x86_64|arm64)\n.+" "\\1" _qt_build_arch "${_qt_arch_config}")
    set(_qt_target_arch "${_qt_build_arch}")
  endif()

  if(NOT _qt_build_arch MATCHES "x86_64|arm64" OR NOT _qt_target_arch MATCHES "x86_64|arm64")
    message(FATAL_ERROR "Unable to detect host or target architecture from Qt dependencies in '${_HQCC_DIRECTORY}'")
  endif()

  string(REPLACE "x86_64" "x64" _qt_build_arch "${_qt_build_arch}")
  string(REPLACE "x86_64" "x64" _qt_target_arch "${_qt_target_arch}")

  set(_qt_host_arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
  string(REPLACE "AMD64" "x64" _qt_host_arch "${_qt_host_arch}")
  string(REPLACE "x86_64" "x64" _qt_host_arch "${_qt_host_arch}")
  string(REPLACE "aarch64" "arm64" _qt_host_arch "${_qt_host_arch}")
  string(REPLACE "ARM64" "arm64" _qt_host_arch "${_qt_host_arch}")

  if(NOT _qt_cross_compiled)
    if(_qt_host_arch STREQUAL _qt_target_arch OR (_qt_host_arch STREQUAL arm64 AND _qt_target_arch STREQUAL x64))
      unset(QT_HOST_PATH CACHE)
      unset(QT_REQUIRE_HOST_PATH_CHECK CACHE)
      return()
    endif()

    set(QT_REQUIRE_HOST_PATH_CHECK TRUE CACHE STRING "Qt Host Tools Check Required" FORCE)
  endif()

  if(NOT DEFINED QT_HOST_PATH OR QT_HOST_PATH STREQUAL "")
    string(REPLACE "${_qt_target_arch}" "${_qt_host_arch}" _qt_host_tools_directory "${_HQCC_DIRECTORY}")

    if(NOT IS_DIRECTORY "${_qt_host_tools_directory}")
      _download_windows_qt_host_tools("${_qt_host_arch}" _qt_host_tools_directory)
    endif()

    if(NOT IS_DIRECTORY "${_qt_host_tools_directory}")
      message(
        FATAL_ERROR
        "Required Qt host tools for ${_qt_host_arch} when building for ${_qt_target_arch} not found in '${_qt_host_tools_directory}'"
      )
    endif()

    set(QT_HOST_PATH "${_qt_host_tools_directory}" CACHE PATH "Qt Host Tools Path" FORCE)
    message(STATUS "Using Qt host tools from ${QT_HOST_PATH}")
  endif()
endfunction()

# _check_dependencies_windows: Set up Windows slice for _check_dependencies
function(_check_dependencies_windows)
  set(arch ${CMAKE_VS_PLATFORM_NAME})
  string(TOLOWER "${arch}" arch_lower)
  set(platform windows-${arch_lower})

  set(dependencies_dir "${CMAKE_CURRENT_SOURCE_DIR}/.deps")
  set(prebuilt_filename "windows-deps-VERSION-ARCH-REVISION.zip")
  set(prebuilt_destination "obs-deps-VERSION-ARCH")
  set(qt6_filename "windows-deps-qt6-VERSION-ARCH-REVISION.zip")
  set(qt6_destination "obs-deps-qt6-VERSION-ARCH")
  set(obs-studio_filename "VERSION.zip")
  set(obs-studio_destination "obs-studio-VERSION")

  # Substitute ARCH tokens with the lowercase arch for filename/path lookups,
  # then restore arch to its original casing for _setup_obs_studio (which passes
  # it to CMake's -A flag, where MSVC requires the canonical casing, e.g. ARM64).
  foreach(var IN ITEMS prebuilt_filename prebuilt_destination qt6_filename qt6_destination)
    string(REPLACE "ARCH" "${arch_lower}" ${var} "${${var}}")
  endforeach()

  # Only download Qt6 if ENABLE_QT is ON
  if(ENABLE_QT)
    set(dependencies_list prebuilt qt6 obs-studio)
  else()
    set(dependencies_list prebuilt obs-studio)
  endif()

  _check_dependencies()

  if(ENABLE_QT)
    _handle_qt_cross_compile(DIRECTORY "${dependencies_dir}/${qt6_destination}")
  endif()
endfunction()

_check_dependencies_windows()
