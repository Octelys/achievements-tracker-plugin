# OBS Achievements Tracker

## MVP

### Configuration

1. Authenticates Xbox Live: gets an XSTS
2. Select Font, Color and Size

### Operation
1. Poll / Loop to get the current game being played
2. Poll / Get to get the achievements
3. Display the number of achievements unlocked

- Switch between Gamer points / Achievements? 
- Share feature (will require a server)?

## Setup

### Prerequisites
- OBS Studio
- Xbox Live account
- Xbox Live API access

### Steps to Build * Run on macOS
1. Clone the repository
2. On macOS, run `cmake --preset macos-dev`
3. Run `cd build_macos_dev`
4. Run `xcodebuild -configuration RelWithDebInfo -scheme achievements-tracker -parallelizeTargets -destination "generic/platform=macOS,name=Any Mac"`
5. Copy the binaries to your OBS Studio plugins folder
6. Start OBS Studio (`open frontend/RelWithDebInfo/OBS.app`)

### Run the tests

For local development on macOS, use the helper script that handles code signing:

```bash
cmake --preset macos-dev
cmake --build build_macos_dev --target test_encoder --config RelWithDebInfo
./scripts/run-tests.sh
```

For CI builds (which use universal OpenSSL), use ctest directly:

```bash
cmake --preset macos-ci -DBUILD_TESTING=ON
cmake --build build_macos --target test_encoder --config RelWithDebInfo
ctest --test-dir build_macos -C RelWithDebInfo --output-on-failure
```
