# CMake Linux build dependencies module

include_guard(GLOBAL)

include(buildspec_common)

# _check_dependencies_linux: Set up Linux slice for _check_dependencies
function(_check_dependencies_linux)
  set(arch ${CMAKE_SYSTEM_PROCESSOR})
  set(platform ubuntu-${arch})

  set(dependencies_dir "${CMAKE_CURRENT_SOURCE_DIR}/.deps")
  set(obs-studio_filename "VERSION.tar.gz")
  set(obs-studio_destination "obs-studio-VERSION")
  set(dependencies_list obs-studio)

  _check_dependencies()
endfunction()

_check_dependencies_linux()
