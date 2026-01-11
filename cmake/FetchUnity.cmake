include(FetchContent)

# Keep Unity pinned for reproducibility.
# Use EXCLUDE_FROM_ALL to prevent Unity's install rules from running.
# We only need Unity for compiling tests, not for distribution.
FetchContent_Declare(unity GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git GIT_TAG v2.5.2 EXCLUDE_FROM_ALL)

# Fetch Unity sources
FetchContent_MakeAvailable(unity)
