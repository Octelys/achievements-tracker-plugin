# Development Guide

This guide covers building, testing, and contributing to the OBS Achievements Tracker Plugin.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Getting the Source](#getting-the-source)
- [Building](#building)
  - [macOS](#macos)
  - [Windows](#windows)
  - [Linux](#linux)
- [Testing](#testing)
- [Code Style](#code-style)
- [Project Structure](#project-structure)
- [Contributing](#contributing)
- [Debugging](#debugging)

## Prerequisites

### All Platforms

- **CMake** 3.28 or later
- **OBS Studio** 30.0+ (headers + libraries)
- **OpenSSL** 3.x (for cryptographic operations)
- **libcurl** (for HTTP requests)
- C compiler with C11 support

### Platform-Specific

#### macOS
- **Xcode Command Line Tools** or **Xcode**
- **Homebrew** (recommended for dependencies)

#### Windows
- **Visual Studio 2022** or later with C/C++ desktop development workload
- **vcpkg** (for dependency management)

#### Linux
- **GCC** or **Clang**
- **libuuid-dev** (for UUID generation)
- **libssl-dev** (OpenSSL development headers)
- **libcurl4-openssl-dev** (libcurl with OpenSSL support)

## Getting the Source

Clone the repository:

```bash
git clone https://github.com/Octelys/achievements-tracker-plugin.git
cd achievements-tracker-plugin
```

## Building

### macOS

#### Development Build (Native Architecture)

For local development, use the `macos-dev` preset which builds for your native architecture and uses Homebrew dependencies:

1. **Install Dependencies:**
   ```bash
   brew install cmake openssl@3 curl libwebsockets
   ```

2. **Configure:**
   ```bash
   cmake --preset macos-dev
   ```

3. **Build:**
   ```bash
   cmake --build build_macos_dev --config Debug
   ```

4. **Install to OBS:**
   ```bash
   cp -r build_macos_dev/Debug/achievements-tracker.plugin \
      ~/Library/Application\ Support/obs-studio/plugins/
   ```

#### CI Build (Universal Binary)

For distribution, use the `macos-ci` preset which creates a universal binary (arm64 + x86_64):

1. **Build Universal OpenSSL:**
   
   The CI preset requires a universal OpenSSL build. The build scripts can create one:
   ```bash
   # This is typically done in CI, but you can run it locally
   ./scripts/build-universal-openssl.sh
   ```

2. **Configure:**
   ```bash
   cmake --preset macos-ci
   ```

3. **Build:**
   ```bash
   cmake --build build_macos --config RelWithDebInfo
   ```

The universal bundle will be at `build_macos/RelWithDebInfo/achievements-tracker.plugin`.

### Windows

**Note:** Windows build support is currently untested. These instructions are based on the build system configuration.

1. **Install Dependencies via vcpkg:**
   ```powershell
   vcpkg install openssl:x64-windows curl:x64-windows libwebsockets:x64-windows
   ```

2. **Configure:**
   ```powershell
   cmake --preset windows-x64
   ```

3. **Build:**
   ```powershell
   cmake --build build_windows --config Release
   ```

4. **Install to OBS:**
   ```powershell
   copy build_windows\Release\achievements-tracker.dll "%ProgramFiles%\obs-studio\obs-plugins\64bit\"
   ```

### Linux

**Note:** Linux build support is currently untested.

1. **Install Dependencies:**

   **Debian/Ubuntu:**
   ```bash
   sudo apt-get update
   sudo apt-get install cmake libssl-dev libcurl4-openssl-dev uuid-dev libwebsockets-dev
   ```

   **Fedora/RHEL:**
   ```bash
   sudo dnf install cmake openssl-devel libcurl-devel libuuid-devel libwebsockets-devel
   ```

2. **Configure:**
   ```bash
   cmake --preset linux-x64
   ```

3. **Build:**
   ```bash
   cmake --build build_linux --config Release
   ```

4. **Install to OBS:**
   ```bash
   mkdir -p ~/.config/obs-studio/plugins
   cp -r build_linux/Release/achievements-tracker.so ~/.config/obs-studio/plugins/
   ```

## Testing

The project uses the [Unity](https://github.com/ThrowTheSwitch/Unity) test framework for unit testing.

### Running All Tests

**macOS (Development):**
```bash
cmake --preset macos-dev -DBUILD_TESTING=ON
cmake --build build_macos_dev --config Debug
ctest --test-dir build_macos_dev -C Debug --output-on-failure
```

**macOS (CI):**
```bash
cmake --preset macos-ci -DBUILD_TESTING=ON
cmake --build build_macos --config RelWithDebInfo
ctest --test-dir build_macos -C RelWithDebInfo --output-on-failure
```

**Windows/Linux:**
```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

### Running Individual Tests

Build and run a specific test:

```bash
# Base64 encoding tests
cmake --build build_macos_dev --target test_encoder --config Debug
./build_macos_dev/Debug/test_encoder

# Cryptographic signing tests
cmake --build build_macos_dev --target test_crypto --config Debug
./build_macos_dev/Debug/test_crypto

# ISO-8601 timestamp parsing tests
cmake --build build_macos_dev --target test_time --config Debug
./build_macos_dev/Debug/test_time

# Text parser tests
cmake --build build_macos_dev --target test_parsers --config Debug
./build_macos_dev/Debug/test_parsers

# Xbox session tests
cmake --build build_macos_dev --target test_xbox_session --config Debug
./build_macos_dev/Debug/test_xbox_session

# Common types tests
cmake --build build_macos_dev --target test_types --config Debug
./build_macos_dev/Debug/test_types
```

### Test Coverage

Code coverage is supported on GCC and Clang:

```bash
cmake --preset macos-dev -DBUILD_TESTING=ON -DENABLE_COVERAGE=ON
cmake --build build_macos_dev --config Debug
ctest --test-dir build_macos_dev -C Debug --output-on-failure

# Generate coverage report (requires lcov)
cmake --build build_macos_dev --target coverage
```

The coverage report will be in `build_macos_dev/coverage/index.html`.

## Code Style

### Formatting

The project uses **ClangFormat** for C code formatting:

```bash
# Format all source files
clang-format -i src/**/*.c src/**/*.h

# Check formatting without modifying files
clang-format --dry-run --Werror src/**/*.c src/**/*.h
```

Configuration is in `.clang-format`.

### CMake Formatting

CMake files use **gersemi** for formatting:

```bash
# Install gersemi
pip install gersemi

# Format CMake files
gersemi -i CMakeLists.txt cmake/*.cmake

# Check formatting
gersemi --check CMakeLists.txt cmake/*.cmake
```

Configuration is in `.gersemirc`.

### Coding Conventions

- **Naming:**
  - Functions: `snake_case`
  - Types: `snake_case_t` (with `_t` suffix)
  - Macros: `UPPER_CASE`
  - Constants: `UPPER_CASE`

- **Memory Management:**
  - Use OBS memory functions: `bmalloc()`, `bfree()`, `bzalloc()`
  - Always free allocated memory
  - Use helper macros: `FREE(ptr)`, `free_memory(ptr, size)`

- **Error Handling:**
  - Check return values
  - Use `blog()` for logging
  - Return appropriate error codes

- **Documentation:**
  - Document public APIs
  - Explain non-obvious code
  - Keep comments up-to-date

## Project Structure

```
achievements-tracker-plugin/
├── src/                      # Source code
│   ├── main.c                # Plugin entry point
│   ├── common/               # Shared types and utilities
│   ├── crypto/               # Cryptographic operations
│   ├── diagnostics/          # Logging
│   ├── drawing/              # Image rendering
│   ├── encoding/             # Base64 encoding
│   ├── io/                   # State persistence
│   ├── net/                  # Networking (HTTP, JSON, browser)
│   ├── oauth/                # OAuth authentication
│   ├── sources/xbox/         # OBS source implementations
│   ├── text/                 # Text parsing
│   ├── time/                 # Time utilities
│   ├── util/                 # General utilities
│   └── xbox/                 # Xbox Live client and session
├── test/                     # Unit tests
│   ├── stubs/                # Test stubs and mocks
│   └── test_*.c              # Test files
├── cmake/                    # CMake modules and helpers
├── data/                     # Plugin data files
│   └── locale/               # Localization files
├── external/                 # Vendored dependencies
│   └── cjson/                # cJSON library
├── scripts/                  # Build and utility scripts
├── CMakeLists.txt            # Main CMake configuration
├── CMakePresets.json         # CMake presets for different platforms
├── .clang-format             # ClangFormat configuration
└── .gersemirc                # Gersemi (CMake formatter) configuration
```

## Contributing

### Workflow

1. **Fork the Repository**
2. **Create a Feature Branch:**
   ```bash
   git checkout -b feature/my-new-feature
   ```
3. **Make Changes**
4. **Run Tests:**
   ```bash
   ctest --test-dir build_macos_dev -C Debug --output-on-failure
   ```
5. **Format Code:**
   ```bash
   clang-format -i src/**/*.c src/**/*.h
   ```
6. **Commit Changes:**
   ```bash
   git commit -m "Add my new feature"
   ```
7. **Push to Fork:**
   ```bash
   git push origin feature/my-new-feature
   ```
8. **Open Pull Request**

### Pull Request Guidelines

- **Describe Your Changes:** Clearly explain what your PR does and why
- **Link Issues:** Reference related issues (e.g., "Fixes #123")
- **Add Tests:** Include tests for new functionality
- **Update Documentation:** Update relevant wiki pages or README
- **Follow Code Style:** Use ClangFormat and follow project conventions
- **Keep PRs Focused:** One feature or fix per PR

### Reporting Issues

When reporting issues, include:
- **OBS Studio Version**
- **Plugin Version**
- **Operating System and Version**
- **Steps to Reproduce**
- **Expected vs. Actual Behavior**
- **Log Files** (from OBS Help → Log Files)

## Debugging

### Enabling Debug Logging

The plugin uses OBS's logging system. To enable debug logs:

1. In OBS Studio, go to **Help → Log Files → Upload Current Log**
2. Or check the log file directly:
   - **macOS:** `~/Library/Application Support/obs-studio/logs/`
   - **Windows:** `%APPDATA%\obs-studio\logs\`
   - **Linux:** `~/.config/obs-studio/logs/`

### Debugging with LLDB (macOS)

```bash
# Build with debug symbols
cmake --preset macos-dev
cmake --build build_macos_dev --config Debug

# Launch OBS with LLDB
lldb /Applications/OBS.app/Contents/MacOS/OBS
(lldb) run
```

### Debugging with GDB (Linux)

```bash
# Build with debug symbols
cmake --preset linux-x64
cmake --build build_linux --config Debug

# Launch OBS with GDB
gdb obs
(gdb) run
```

### Debugging with Visual Studio (Windows)

1. Open the solution in Visual Studio
2. Set OBS.exe as the debugging target
3. Set breakpoints in plugin code
4. Press F5 to start debugging

### Common Debugging Scenarios

#### Authentication Issues
- Check `state.json` for token validity
- Verify timestamps are not expired
- Check network connectivity
- Review OAuth flow logs

#### API Failures
- Verify XSTS token is valid
- Check API endpoint URLs
- Review HTTP response codes and bodies
- Ensure proper authorization headers

#### Rendering Issues
- Check OBS graphics context
- Verify image/texture creation
- Review source update callbacks
- Check for null pointers

### Useful Tools

- **Wireshark/Charles Proxy:** Monitor HTTP/HTTPS traffic
- **Postman/Insomnia:** Test Xbox Live APIs manually
- **Valgrind (Linux):** Memory leak detection
- **Instruments (macOS):** Profiling and memory analysis
- **Visual Studio Profiler (Windows):** Performance analysis

## Building for Distribution

### macOS Package

```bash
# Build universal binary
cmake --preset macos-ci
cmake --build build_macos --config RelWithDebInfo

# Create DMG (if packaging script exists)
./scripts/package-macos.sh
```

### Windows Package

```bash
# Build release
cmake --preset windows-x64
cmake --build build_windows --config Release

# Create installer (if packaging script exists)
./scripts/package-windows.bat
```

## Continuous Integration

The project uses GitHub Actions for CI/CD:

- **Build:** Compile for all platforms
- **Test:** Run unit tests
- **Lint:** Check code formatting
- **Package:** Create distribution packages
- **Release:** Publish releases to GitHub

See `.github/workflows/` for CI configuration.

## Additional Resources

- [OBS Studio Plugin Development](https://obsproject.com/docs/plugins.html)
- [OBS Studio API Documentation](https://obsproject.com/docs/)
- [CMake Documentation](https://cmake.org/documentation/)
- [Unity Test Framework](https://github.com/ThrowTheSwitch/Unity)
- [Xbox Live API Reference](https://learn.microsoft.com/en-us/gaming/xbox-live/api-ref/)

## Getting Help

- **GitHub Issues:** Report bugs and request features
- **GitHub Discussions:** Ask questions and share ideas
- **OBS Discord:** Join the OBS community
- **Xbox Live Documentation:** Official API documentation

## License

This project is licensed under the GPL-2.0 License. See [LICENSE](../LICENSE) for details.
