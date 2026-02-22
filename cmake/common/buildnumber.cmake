# CMake build number module

include_guard(GLOBAL)

# Resolve build number (prefer CI-provided sequential run number)
if(NOT DEFINED PLUGIN_BUILD_NUMBER)
  if("$ENV{GITHUB_RUN_NUMBER}")
    # GitHub Actions: sequential, human-readable run number
    set(PLUGIN_BUILD_NUMBER "$ENV{GITHUB_RUN_NUMBER}")
  elseif("$ENV{GITLAB_PIPELINE_IID}")
    # GitLab CI: project-scoped sequential pipeline number
    set(PLUGIN_BUILD_NUMBER "$ENV{GITLAB_PIPELINE_IID}")
  else()
    # Local / offline build
    set(PLUGIN_BUILD_NUMBER "0")
  endif()
endif()
