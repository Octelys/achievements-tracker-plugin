# CMake macOS build dependencies module

include_guard(GLOBAL)

include(buildspec_common)

# _check_dependencies_macos: Set up macOS slice for _check_dependencies
function(_check_dependencies_macos)
  set(arch universal)
  set(platform macos)

  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" buildspec)

  set(dependencies_dir "${CMAKE_CURRENT_SOURCE_DIR}/.deps")
  set(prebuilt_filename "macos-deps-VERSION-ARCH_REVISION.tar.xz")
  set(prebuilt_destination "obs-deps-VERSION-ARCH")
  set(qt6_filename "macos-deps-qt6-VERSION-ARCH-REVISION.tar.xz")
  set(qt6_destination "obs-deps-qt6-VERSION-ARCH")
  set(obs-studio_filename "VERSION.tar.gz")
  set(obs-studio_destination "obs-studio-VERSION")

  # Only download Qt6 if ENABLE_QT is ON
  if(ENABLE_QT)
    set(dependencies_list prebuilt qt6 obs-studio)
  else()
    set(dependencies_list prebuilt obs-studio)
  endif()

  _check_dependencies()

  # Remove quarantine attributes from downloaded dependencies only
  # Use glob to only process known dependency directories, not build artifacts
  file(
    GLOB dep_dirs
    "${dependencies_dir}/obs-deps-*"
    "${dependencies_dir}/obs-studio-*"
    "${dependencies_dir}/Frameworks"
    "${dependencies_dir}/openssl-universal"
    "${dependencies_dir}/freetype-universal"
  )

  foreach(dep_dir ${dep_dirs})
    if(EXISTS "${dep_dir}")
      execute_process(
        COMMAND "xattr" -r -d com.apple.quarantine "${dep_dir}"
        RESULT_VARIABLE xattr_result
        ERROR_QUIET
      )
      # Don't fail the build if xattr fails - it's not critical
      if(NOT xattr_result EQUAL 0)
        message(STATUS "Note: Could not remove quarantine attribute from ${dep_dir} (not critical)")
      endif()
    endif()
  endforeach()

  list(APPEND CMAKE_FRAMEWORK_PATH "${dependencies_dir}/Frameworks")
  set(CMAKE_FRAMEWORK_PATH ${CMAKE_FRAMEWORK_PATH} PARENT_SCOPE)
endfunction()

_check_dependencies_macos()
