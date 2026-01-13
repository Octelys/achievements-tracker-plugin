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
   - Add a new **Source** → **Achievements Tracker**
   - Click the **Sign in with Xbox Live** button in the source properties
   - A browser window will open; sign in with your Microsoft account
   - Grant the requested permissions
   - The plugin will automatically retrieve and cache your XSTS token

2. **Customize Appearance** (Soon) : 
   - **Font**: Select font family and size
   - **Color**: Choose text color
   - **Display Mode**: Toggle between achievements count and gamer score

### Usage

Once configured, the source will automatically display:
- **Achievements Mode**: Current game's unlocked achievements count (e.g., "15 / 50 Achievements")
- **Gamer Score Mode**: Total gamer score (e.g., "12,450 G")

The plugin polls Xbox Live periodically to detect:
- The currently active game
- Achievement unlock events
- Updated gamer score

To switch between display modes, open the source properties and change the **Display Mode** dropdown.

---

## Developer Documentation

### Repository Structure

```
achievements-tracker-plugin/
├── src/
│   ├── main.c                          # Plugin entry point, module registration
│   ├── sources/
│   │   └── xbox_achievements_text_source.c  # OBS text source implementation
│   ├── oauth/
│   │   ├── xbox-live.c                 # Xbox Live OAuth & XSTS token flows
│   │   └── util.c                      # OAuth helpers (PKCE, code exchange)
│   ├── crypto/
│   │   └── crypto.c                    # EC key generation, signing (Proof-of-Possession)
│   ├── net/
│   │   ├── http/http.c                 # libcurl HTTP GET/POST wrappers
│   │   ├── json/json.c                 # Minimal JSON parsing helpers
│   │   └── browser/browser.c           # Platform-specific browser launch
│   ├── io/
│   │   └── state.c                     # Persistent state (tokens, device keys)
│   ├── xbox/
│   │   └── client.c                    # Xbox Live API client (profile, achievements)
│   ├── encoding/
│   │   └── base64.c                    # Base64 URL-safe encoding
│   ├── time/
│   │   └── time.c                      # ISO-8601 timestamp parsing
│   └── util/
│       └── uuid.c                      # Cross-platform UUID generation
├── test/
│   ├── test_encoder.c                  # Base64 encoding tests
│   ├── test_crypto.c                   # Cryptographic signing tests
│   ├── test_time.c                     # ISO-8601 parsing tests
│   └── stubs/                          # Test stubs (bmem_stub.c, etc.)
├── cmake/                              # Build configuration helpers
├── data/locale/                        # Localization files
└── CMakeLists.txt                      # Main build configuration
```

### OAuth Authentication Sequence

The plugin implements the Xbox Live authentication flow with Proof-of-Possession (PoP) signing:

#### 1. **Microsoft OAuth 2.0 (Authorization Code Flow with PKCE)**
   - Generate PKCE `code_verifier` and `code_challenge`
   - Open system browser to Microsoft login: `https://login.microsoftonline.com/consumers/oauth2/v2.0/authorize`
   - User signs in and consents
   - Microsoft redirects to `http://localhost:<port>/callback?code=...`
   - Plugin exchanges `code` for an **Access Token** via `https://login.microsoftonline.com/consumers/oauth2/v2.0/token`

#### 2. **Xbox Live Device Token**
   - Generate an EC P-256 key pair (device Proof-of-Possession key)
   - POST to `https://device.auth.xboxlive.com/device/authenticate` with:
     - Device UUID (random, cached)
     - Public key (JWK format)
     - Signature header (signed with private key)
   - Receive **Device Token**

#### 3. **Xbox Live SISU Authorization** (Single Sign-In, Single Sign-Out)
   - POST to `https://sisu.xboxlive.com/authorize` with:
     - Microsoft Access Token
     - Device Token
     - Public key (JWK)
     - Signature header
   - Receive:
     - **User Token**
     - **Title Token**
     - **XSTS Token** (Xbox Secure Token Service token)
     - User identity: `gamertag`, `xuid` (Xbox User ID), `uhs` (User Hash)

#### 4. **Token Storage & Refresh**
   - All tokens are cached in `~/.config/obs-studio/plugin_config/achievements-tracker/state.json`
   - Device keys are persisted (EC private key, PEM format)
   - XSTS token is used for subsequent Xbox Live API calls

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
- **libuuid** (Linux/BSD only, for UUID generation)
- C compiler with C11 support

### Platform-Specific Setup

#### macOS

1. Install dependencies via Homebrew:
   ```bash
   brew install cmake openssl@3 curl
   ```

2. Clone and configure:
   ```bash
   git clone https://github.com/your-org/achievements-tracker-plugin.git
   cd achievements-tracker-plugin
   cmake --preset macos-dev
   ```

3. Build:
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

#### Windows

1. Install dependencies via vcpkg:
   ```powershell
   vcpkg install openssl:x64-windows curl:x64-windows
   ```

2. Configure and build:
   ```powershell
   cmake --preset windows-x64
   cmake --build build_windows --config Release
   ```

#### Linux

1. Install dependencies:
   ```bash
   sudo apt-get install cmake libssl-dev libcurl4-openssl-dev uuid-dev
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

### Windows / Linux

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

### Running Individual Tests

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

## License

[Your License Here]

## Contributing

Contributions are welcome! Please open an issue or submit a pull request.

## Support

For issues, questions, or feature requests, please visit the [GitHub Issues](https://github.com/your-org/achievements-tracker-plugin/issues) page.
