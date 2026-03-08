# CMake Windows build dependencies module

include_guard(GLOBAL)

include(buildspec_common)

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
endfunction()

_check_dependencies_windows()
