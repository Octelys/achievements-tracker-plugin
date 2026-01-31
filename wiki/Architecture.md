# Architecture

This page describes the technical architecture and design of the OBS Achievements Tracker Plugin.

## Overview

The plugin follows a layered architecture with clear separation of concerns:

```
┌─────────────────────────────────────────────────────────┐
│                    OBS Studio                           │
│  ┌──────────────────────────────────────────────────┐  │
│  │           OBS Source Layer                       │  │
│  │  - Xbox Account Source                           │  │
│  │  - Xbox Game Cover Source                        │  │
│  │  - Xbox Gamerscore Source                        │  │
│  └────────────┬─────────────────────────────────────┘  │
└───────────────┼─────────────────────────────────────────┘
                │
┌───────────────▼─────────────────────────────────────────┐
│           Session Management Layer                      │
│  - xbox_session.h: Current game, achievements, state    │
│  - xbox_monitor.h: Real-time activity monitoring        │
└───────────────┬─────────────────────────────────────────┘
                │
┌───────────────▼─────────────────────────────────────────┐
│              Data Access Layer                          │
│  - xbox_client.h: Xbox Live API client                  │
│    • Profile data (gamerscore)                          │
│    • Game information                                   │
│    • Achievement data                                   │
│    • Cover art images                                   │
└───────────────┬─────────────────────────────────────────┘
                │
┌───────────────▼─────────────────────────────────────────┐
│           Authentication Layer                          │
│  - oauth/xbox-live.h: OAuth 2.0 flows                   │
│  - crypto/crypto.h: Proof-of-Possession signing         │
│  - io/state.h: Token persistence                        │
└─────────────────────────────────────────────────────────┘
```

## Component Breakdown

### 1. OBS Source Layer (`src/sources/xbox/`)

The top layer that integrates with OBS Studio's plugin API.

#### `account.c/h`
- Registers the "Xbox Account" source type
- Provides UI for authentication (sign-in button)
- Manages the OAuth flow initiation
- Displays connection status

#### `game_cover.c/h`
- Registers the "Xbox Game Cover" source type
- Fetches and displays game cover art
- Handles image rendering and caching
- Updates when the current game changes

#### `gamerscore.c/h`
- Registers the "Xbox Gamerscore" source type
- Displays the user's total gamerscore
- Formats and renders the score as text/graphics
- Updates on achievement unlocks

**Key Responsibilities:**
- OBS source registration and lifecycle management
- Property configuration UI
- Rendering (text, images) via OBS graphics API
- Event handling from lower layers

### 2. Session Management Layer (`src/xbox/`)

Manages the runtime state and monitors Xbox Live activity.

#### `xbox_session.c/h`
- Central session state container
- Stores current game information
- Caches achievement lists
- Tracks unlocked achievements
- Manages xbox_identity_t (gamertag, xuid, auth tokens)

#### `xbox_monitor.c/h`
- Real-time activity monitoring (when libwebsockets is available)
- Polls Xbox Live for state changes
- Detects game switches
- Notifies on achievement unlocks
- Manages WebSocket connections to Xbox Live RTA service

**Key Responsibilities:**
- In-memory state management
- Change detection and notification
- Coordination between data layer and sources
- Background monitoring threads/callbacks

### 3. Data Access Layer (`src/xbox/`)

Handles communication with Xbox Live REST APIs.

#### `xbox_client.c/h`
- **Profile API**: Fetch gamerscore and profile settings
- **Achievements API**: Get achievement lists for games
- **Presence API**: Determine currently active game
- **Cover Art API**: Download game cover images
- HTTP request construction with proper headers
- Response parsing (JSON)
- Error handling and retry logic

**Key Responsibilities:**
- Xbox Live API wrapper functions
- HTTP request/response handling
- JSON parsing and data extraction
- API-specific logic (endpoints, parameters, headers)

### 4. Authentication Layer (`src/oauth/`, `src/crypto/`, `src/io/`)

Manages Xbox Live authentication and authorization.

#### `oauth/xbox-live.c/h`
- **Device Code Flow**: Microsoft OAuth 2.0 authentication
- **Device Authentication**: Xbox Live device token acquisition
- **SISU Authorization**: Single Sign-In, Single Sign-Out flow
- **Token Refresh**: Automatic token renewal
- Asynchronous authentication state machine

#### `crypto/crypto.c/h`
- **EC Key Generation**: P-256 elliptic curve keys for PoP
- **Request Signing**: Signs API requests with private key
- **JWK Export**: Converts keys to JSON Web Key format
- Uses OpenSSL for cryptographic operations

#### `io/state.c/h`
- **Token Persistence**: Saves/loads authentication tokens
- **Device Key Storage**: Persists EC private key (PEM format)
- **State File**: `~/.config/obs-studio/plugin_config/achievements-tracker/state.json`
- Atomic file operations for data integrity

**Key Responsibilities:**
- OAuth 2.0 authentication flows
- Cryptographic operations for Proof-of-Possession
- Secure token storage and retrieval
- Authentication state persistence

## Supporting Modules

### Common Types (`src/common/`)
Shared data structures used throughout the plugin:
- `achievement_t`: Achievement metadata
- `achievement_progress_t`: Progress tracking
- `game_t`: Game descriptor (ID, title, cover URL)
- `gamerscore_t`: Gamerscore container
- `token_t`: Authentication token with expiration
- `unlocked_achievement_t`: Unlocked achievement record
- `xbox_identity_t`: User identity (gamertag, xuid, tokens)
- `xbox_session_t`: Session state

### Networking (`src/net/`)
- `http/http.c/h`: libcurl HTTP GET/POST wrappers
- `json/json.c/h`: JSON parsing helpers (wraps cJSON)
- `browser/browser.c/h`: Platform-specific browser launch

### Utilities
- `encoding/base64.c/h`: URL-safe Base64 encoding
- `text/parsers.c/h`: Text parsing utilities
- `time/time.c/h`: ISO-8601 timestamp parsing
- `util/uuid.c/h`: Cross-platform UUID generation
- `drawing/image.c/h`: Image rendering helpers

### Diagnostics (`src/diagnostics/`)
- `log.c/h`: Logging wrapper around OBS logging API
- CMake-configured with plugin name and version

## Data Flow

### Authentication Flow
```
1. User clicks "Sign In" in Xbox Account source
   ↓
2. oauth/xbox-live initiates device code flow
   ↓
3. Browser opens to Microsoft login page
   ↓
4. User signs in and authorizes
   ↓
5. Plugin polls token endpoint
   ↓
6. Receives access token and refresh token
   ↓
7. Generates device EC keypair (crypto/crypto)
   ↓
8. Acquires Xbox device token (with PoP)
   ↓
9. Performs SISU authorization
   ↓
10. Receives XSTS token + user identity
    ↓
11. Stores all tokens in state.json (io/state)
    ↓
12. Updates xbox_session with identity
```

### Data Refresh Flow
```
1. xbox_monitor detects state change or timer fires
   ↓
2. xbox_client fetches updated data from Xbox Live APIs
   ↓
3. Parses JSON response
   ↓
4. Updates xbox_session state
   ↓
5. Notifies OBS sources via callbacks
   ↓
6. Sources update their rendering
```

### Rendering Flow
```
1. OBS calls source's video_render() callback
   ↓
2. Source reads current data from xbox_session
   ↓
3. Source renders text/image using OBS graphics API
   ↓
4. Frame is composited into OBS scene
```

## Thread Safety

- **Main Thread**: OBS source callbacks, UI interactions
- **Network Thread**: HTTP requests, API calls (via libcurl)
- **Monitor Thread**: Background polling/WebSocket (when enabled)

Thread synchronization:
- Uses OBS threading primitives (pthread on Unix, Win32 threads on Windows)
- Mutexes protect shared state in xbox_session
- Callbacks to OBS sources always happen on main thread

## Build System

The plugin uses **CMake** as its build system:
- CMake 3.28+ required
- Presets for different platforms (macos-dev, macos-ci, windows-x64, linux-x64)
- Automated dependency resolution (OpenSSL, libcurl, libuuid)
- Universal binary support on macOS (arm64 + x86_64)
- OBS plugin structure (bundle on macOS, DLL on Windows, .so on Linux)

### Build Artifacts
- **macOS**: `achievements-tracker.plugin/` bundle
- **Windows**: `achievements-tracker.dll`
- **Linux**: `achievements-tracker.so`

## Testing

Unit tests use the **Unity** testing framework:
- `test_encoder`: Base64 encoding tests
- `test_crypto`: Cryptographic signing tests
- `test_time`: ISO-8601 parsing tests
- `test_parsers`: Text parsing tests
- `test_xbox_session`: Session management tests
- `test_types`: Common types tests

Tests use stubs for OBS API calls (`bmem_stub.c`) and external dependencies.

## External Dependencies

| Dependency | Purpose | License |
|------------|---------|---------|
| **libobs** | OBS Studio plugin API | GPL-2.0+ |
| **OpenSSL** | Cryptography (EC keys, signing) | Apache 2.0 |
| **libcurl** | HTTP/HTTPS requests | MIT-like |
| **cJSON** | JSON parsing (vendored) | MIT |
| **libuuid** | UUID generation (Unix) | BSD-3-Clause |
| **libwebsockets** | Real-time monitoring (optional) | MIT |

## Design Principles

1. **Separation of Concerns**: Each layer has well-defined responsibilities
2. **Asynchronous Operations**: Network calls don't block the UI thread
3. **Error Resilience**: Graceful degradation when APIs are unavailable
4. **Caching**: Minimize API calls through intelligent caching
5. **Security**: Tokens never exposed in logs or insecure storage
6. **Cross-Platform**: Platform-specific code isolated in dedicated modules
7. **Testability**: Core logic separated from OBS dependencies

## Future Architecture Considerations

- **Plugin Communication**: Potential for inter-plugin messaging
- **Multiple Accounts**: Supporting multiple Xbox accounts simultaneously
- **Custom Sources**: API for third-party extensions
- **Event System**: Pub/sub for achievement unlocks and game changes
- **Offline Mode**: Graceful operation when Xbox Live is unavailable
