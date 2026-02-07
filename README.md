# OBS Achievements Tracker

A cross-platform OBS Studio plugin that displays real-time Xbox Live achievement progress and gamer score for the currently signed-in Xbox user.

## Features

- **Xbox Live Authentication**: Secure OAuth 2.0 authentication flow with Microsoft accounts
- **Real-time Achievement Tracking**: Monitor achievement unlocks as they happen (Soon)
- **Gamer Score Display**: Show total gamer score and progress
- **Customizable Text Source**: Configurable font, color, and size (Soon)
- **Cross-platform**: Supports Windows, macOS, and Linux

---

## User Guide

### Installation

1. Download the latest release for your platform from the [Releases](https://github.com/your-org/achievements-tracker-plugin/releases) page
2. Extract the archive to your OBS Studio plugins directory:
   - **Windows**: `%ProgramFiles%\obs-studio\obs-plugins\64bit\`
   - **macOS**: `~/Library/Application Support/obs-studio/plugins/`
   - **Linux**: `~/.config/obs-studio/plugins/`
3. Restart OBS Studio

### Configuration

1. **Sign in to Xbox Live**:
   - Open OBS Studio
   - Add a new **Source** → **Xbox Account**
   - Click the **Sign in with Xbox Live** button in the source properties
   - A browser window will open; sign in with your Microsoft account
   - Grant the requested permissions
   - The plugin will automatically retrieve and cache your XSTS token

### Usage

Once configured, the source will automatically display:
- Using the **Xbox Cover** source, the cover of the game currently active on your Xbox One console will be shown
- Using the **Xbox Gamerscore** source: your current gamerscore will be shown
- TBD **Achievements Mode**: Current game's unlocked achievements count (e.g., "15 / 50 Achievements")

The plugin subscribes Xbox Live to get real-time updates of:
- The currently active game
- Achievement unlock events

---

## Developer Documentation

### Repository Structure

```
achievements-tracker-plugin/
├── src/
│   ├── main.c                          # Plugin entry point, module registration
│   ├── common/                         # Shared data types and utilities
│   │   ├── achievement.c/h             # Achievement type (copy/free/count)
│   │   ├── achievement_progress.c/h    # Achievement progress tracking
│   │   ├── device.h                    # Device identity (UUID, keys)
│   │   ├── game.c/h                    # Game descriptor (id, title)
│   │   ├── gamerscore.c/h              # Gamerscore container & computation
│   │   ├── memory.h                    # Memory allocation helpers (FREE, free_memory)
│   │   ├── token.c/h                   # Auth token with expiration
│   │   ├── types.h                     # Umbrella header, macros, platform helpers
│   │   ├── unlocked_achievement.c/h    # Unlocked achievement tracking
│   │   ├── xbox_identity.c/h           # Xbox identity (gamertag, xuid, uhs, token)
│   │   └── xbox_session.c/h            # Xbox session state container
│   ├── crypto/
│   │   └── crypto.c/h                  # EC key generation, signing (Proof-of-Possession)
│   ├── diagnostics/
│   │   ├── log.c.in                    # Logging (CMake-configured)
│   │   └── log.h                       # Logging API
│   ├── drawing/
│   │   ├── image.c/h                   # Image rendering helpers
│   ├── encoding/
│   │   └── base64.c/h                  # Base64 URL-safe encoding
│   ├── io/
│   │   └── state.c/h                   # Persistent state (tokens, device keys)
│   ├── net/
│   │   ├── browser/browser.c/h         # Platform-specific browser launch
│   │   ├── http/http.c/h               # libcurl HTTP GET/POST wrappers
│   │   └── json/json.c/h               # JSON parsing helpers
│   ├── oauth/
│   │   ├── util.c/h                    # OAuth helpers (PKCE, code exchange)
│   │   └── xbox-live.c/h               # Xbox Live OAuth & XSTS token flows
│   ├── sources/xbox/                   # OBS source implementations
│   │   ├── account.c/h                 # Xbox Account source
│   │   ├── game_cover.c/h              # Xbox Game Cover source
│   │   └── gamerscore.c/h              # Xbox Gamerscore source
│   ├── text/
│   │   └── parsers.c/h                 # Text/response parsing utilities
│   ├── time/
│   │   └── time.c/h                    # ISO-8601 timestamp parsing
│   ├── util/
│   │   └── uuid.c/h                    # Cross-platform UUID generation
│   └── xbox/
│       ├── xbox_client.c/h             # Xbox Live API client (profile, achievements)
│       ├── xbox_monitor.c/h            # Real-time activity monitoring
│       └── xbox_session.c/h            # Session management
├── test/
│   ├── test_crypto.c                   # Cryptographic signing tests
│   ├── test_encoder.c                  # Base64 encoding tests
│   ├── test_parsers.c                  # Text parser tests
│   ├── test_time.c                     # ISO-8601 parsing tests
│   ├── test_types.c                    # Common types tests
│   ├── test_xbox_session.c             # Xbox session tests
│   ├── unity_config.h                  # Unity test framework config
│   └── stubs/                          # Test stubs (bmem_stub.c, mocks, etc.)
├── cmake/                              # Build configuration helpers
├── data/locale/                        # Localization files
├── external/cjson/                     # cJSON library (vendored)
└── CMakeLists.txt                      # Main build configuration
```

### OAuth Authentication Sequence

The plugin implements the Xbox Live authentication flow with Proof-of-Possession (PoP) signing:

#### 1. **Microsoft OAuth 2.0 (Device Code Flow)**
   - POST to `https://login.live.com/oauth20_connect.srf` with:
     - `client_id`: Application client ID
     - `response_type`: `device_code`
     - `scope`: `service::user.auth.xboxlive.com::MBI_SSL`
   - Receive `device_code`, `user_code`, `interval`, and `expires_in`
   - Open system browser to `https://login.live.com/oauth20_remoteconnect.srf?otc=<user_code>`
   - User signs in and enters the displayed code
   - Plugin polls `https://login.live.com/oauth20_token.srf` at the specified interval until:
     - User completes authentication → receive **Access Token** and **Refresh Token**
     - Or timeout expires

#### 2. **Xbox Live Device Token**
   - Generate an EC P-256 key pair (device Proof-of-Possession key)
   - POST to `https://device.auth.xboxlive.com/device/authenticate` with:
     - Device UUID and serial number (random, cached)
     - Public key (JWK format) in `ProofKey`
     - `signature` header (request signed with private key)
   - Receive **Device Token** (JWT) with expiration (`NotAfter`)

#### 3. **Xbox Live SISU Authorization** (Single Sign-In, Single Sign-Out)
   - POST to `https://sisu.xboxlive.com/authorize` with:
     - Microsoft Access Token (as `AccessToken`: `t=<token>`)
     - Device Token
     - App ID (client ID)
     - Public key (JWK) in `ProofKey`
     - `signature` header (request signed with private key)
   - Receive:
     - **Authorization Token** (XSTS token at `AuthorizationToken.Token`)
     - User identity: `gtg` (gamertag), `xid` (Xbox User ID), `uhs` (User Hash)
     - Token expiration (`NotAfter`)

#### 4. **Token Storage & Refresh**
   - All tokens are cached in `~/.config/obs-studio/plugin_config/achievements-tracker/state.json`
   - Device keys are persisted (EC private key, PEM format)
   - On startup:
     1. If valid **User Token** exists → use it
     2. Else if **Refresh Token** exists → exchange for new Access Token via token endpoint
     3. Else → start Device Code Flow from step 1
   - Device Token is also cached and reused if still valid

#### 5. **Authenticated API Calls**
   - All Xbox Live API requests include the header:
     ```
     Authorization: XBL3.0 x=<uhs>;<xsts_token>
     ```
   - Examples:
     - **Profile**: `GET https://profile.xboxlive.com/users/xuid(<xuid>)/profile/settings?settings=Gamerscore`
     - **Achievements**: `GET https://achievements.xboxlive.com/users/xuid(<xuid>)/achievements`

---

## Building from Source

### Prerequisites

- **CMake** 3.28 or later
- **OBS Studio** 30.0+ (headers + libraries)
- **OpenSSL** 3.x (for cryptographic operations)
- **libcurl** (for HTTP requests)
- **FreeType** 2.x (for text rendering)
- **zlib** (required by FreeType, usually available by default)
- **libuuid** (Linux/BSD only, for UUID generation)
- C compiler with C11 support

**Note on zlib:** zlib is required by FreeType for font compression support and is typically pre-installed on all platforms (macOS SDK, Windows via vcpkg/obs-deps, Linux system packages).

### Platform-Specific Setup

#### macOS

1. Install dependencies via Homebrew:
   ```bash
   brew install cmake openssl@3 curl freetype
   ```

2. Clone and configure:
   ```bash
   git clone https://github.com/your-org/achievements-tracker-plugin.git
   cd achievements-tracker-plugin
   cmake --preset macos-dev
   ```

3. Build:
   ```bash
   xcodebuild -configuration RelWithDebInfo -scheme achievements-tracker -parallelizeTargets -destination "generic/platform=macOS,name=Any Mac"
   ```
   
Or maybe:

    ```bash
    cmake --build build_macos_dev --config Debug
    ```

4. The plugin bundle will be at:
   ```
   build_macos_dev/Debug/achievements-tracker.plugin
   ```

5. Copy to OBS plugins folder:
   ```bash
   cp -r build_macos_dev/Debug/achievements-tracker.plugin \
      ~/Library/Application\ Support/obs-studio/plugins/
   ```

##### Building Universal Binary for CI

For universal (arm64+x86_64) builds, you'll need a universal FreeType binary:

```bash
# Build universal FreeType (this will be done automatically in CI)
./scripts/build-universal-freetype.sh

# Then configure for universal build
cmake --preset macos-ci
```

The CI workflow automatically builds universal FreeType before building the plugin.

#### Windows (Untested)

1. Install dependencies via vcpkg:
   ```powershell
   vcpkg install openssl:x64-windows curl:x64-windows freetype:x64-windows
   ```

2. Configure and build:
   ```powershell
   cmake --preset windows-x64
   cmake --build build_windows --config Release
   ```

#### Linux (Untested)

1. Install dependencies:
   ```bash
   sudo apt-get install cmake libssl-dev libcurl4-openssl-dev uuid-dev libfreetype6-dev
   ```

2. Configure and build:
   ```bash
   cmake --preset linux-x64
   cmake --build build_linux --config Release
   ```

---

## Running Tests

The project uses [Unity](https://github.com/ThrowTheSwitch/Unity) as the unit testing framework.

### macOS

For local development (single-arch, uses Homebrew OpenSSL):

```bash
cmake --preset macos-dev -DBUILD_TESTING=ON
cmake --build build_macos_dev --config Debug
ctest --test-dir build_macos_dev -C Debug --output-on-failure
```

For CI builds (universal binary, custom OpenSSL):

```bash
cmake --preset macos-ci -DBUILD_TESTING=ON
cmake --build build_macos --config RelWithDebInfo
ctest --test-dir build_macos -C RelWithDebInfo --output-on-failure
```

### Windows / Linux (Untested)

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

### Running Individual Tests (Untested)

```bash
# Test Base64 encoding
cmake --build build_macos_dev --target test_encoder --config Debug
./build_macos_dev/Debug/test_encoder

# Test cryptographic signing
cmake --build build_macos_dev --target test_crypto --config Debug
./build_macos_dev/Debug/test_crypto

# Test ISO-8601 timestamp parsing
cmake --build build_macos_dev --target test_time --config Debug
./build_macos_dev/Debug/test_time
```

---

## References

https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/live/rest/uri/gamerpic/atoc-reference-gamerpic
https://deepwiki.com/microsoft/xbox-live-api/5-real-time-activity-system#resource-uri-format

## Contributing

Contributions are welcome! Please open an issue or submit a pull request.

## Support

For issues, questions, or feature requests, please visit the [GitHub Issues](https://github.com/your-org/achievements-tracker-plugin/issues) page.