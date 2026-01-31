# Authentication Flow

This page provides a detailed explanation of how the Xbox Live authentication process works in the plugin.

## Overview

The plugin implements the Xbox Live authentication flow using OAuth 2.0 with Proof-of-Possession (PoP) token signing. This ensures secure, standards-based authentication with Microsoft accounts.

## Authentication Sequence

The complete authentication flow consists of four main steps:

```
┌─────────────────────────────────────────────────────────────┐
│  Step 1: Microsoft OAuth 2.0 (Device Code Flow)            │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│  Step 2: Xbox Live Device Token                            │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│  Step 3: Xbox Live SISU Authorization                      │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│  Step 4: Token Storage & Refresh                           │
└─────────────────────────────────────────────────────────────┘
```

## Step 1: Microsoft OAuth 2.0 (Device Code Flow)

The plugin uses the OAuth 2.0 Device Authorization Grant flow, which is ideal for devices with limited input capabilities or applications that don't have direct access to a web browser.

### 1.1 Device Authorization Request

**Endpoint:** `POST https://login.live.com/oauth20_connect.srf`

**Request Parameters:**
```
client_id: <Application Client ID>
response_type: device_code
scope: service::user.auth.xboxlive.com::MBI_SSL
```

**Response:**
```json
{
  "device_code": "BAQABAAEAAAAm-...",
  "user_code": "ABCD1234",
  "verification_uri": "https://login.live.com/oauth20_remoteconnect.srf",
  "expires_in": 900,
  "interval": 5
}
```

### 1.2 User Authentication

The plugin opens the system default browser to:
```
https://login.live.com/oauth20_remoteconnect.srf?otc=<user_code>
```

The user:
1. Signs in with their Microsoft account
2. Sees the user code displayed (e.g., "ABCD1234")
3. Grants the requested permissions

### 1.3 Token Polling

The plugin polls the token endpoint at the specified interval:

**Endpoint:** `POST https://login.live.com/oauth20_token.srf`

**Request Parameters:**
```
client_id: <Application Client ID>
grant_type: urn:ietf:params:oauth:grant-type:device_code
device_code: <device_code from step 1.1>
```

**Possible Responses:**

**Authorization Pending:**
```json
{
  "error": "authorization_pending"
}
```
Continue polling...

**Success:**
```json
{
  "token_type": "bearer",
  "scope": "service::user.auth.xboxlive.com::MBI_SSL",
  "expires_in": 3600,
  "access_token": "EwAIA+pvBAAU...",
  "refresh_token": "M.R3_BAY.-CQ...",
  "user_id": "..."
}
```

**Error (Timeout/Denied):**
```json
{
  "error": "expired_token" | "access_denied"
}
```

## Step 2: Xbox Live Device Token

Once we have a Microsoft access token, we need to authenticate as an Xbox device.

### 2.1 Generate Device Identity

The plugin generates and persists:
- **Device UUID**: Random UUID (e.g., `550e8400-e29b-41d4-a716-446655440000`)
- **Device Serial**: Random serial number
- **EC Keypair**: P-256 elliptic curve key pair for Proof-of-Possession

These are cached in the state file and reused across sessions.

### 2.2 Device Authentication Request

**Endpoint:** `POST https://device.auth.xboxlive.com/device/authenticate`

**Headers:**
```
Content-Type: application/json
x-xbl-contract-version: 1
signature: <base64-encoded signature of request>
```

**Request Body:**
```json
{
  "Properties": {
    "AuthMethod": "ProofOfPossession",
    "Id": "{550e8400-e29b-41d4-a716-446655440000}",
    "DeviceType": "Nintendo",
    "SerialNumber": "123456789",
    "Version": "0.0.0.0",
    "ProofKey": {
      "kty": "EC",
      "x": "<base64-url-encoded x coordinate>",
      "y": "<base64-url-encoded y coordinate>",
      "crv": "P-256",
      "alg": "ES256",
      "use": "sig"
    }
  },
  "RelyingParty": "http://auth.xboxlive.com",
  "TokenType": "JWT"
}
```

**Signature Generation:**
The `signature` header is a Base64-encoded ECDSA-SHA256 signature of:
```
<timestamp (Unix epoch)><NUL>POST<NUL>/device/authenticate<NUL><NUL><request_body><NUL>
```

**Response:**
```json
{
  "IssueInstant": "2024-01-01T00:00:00.0000000Z",
  "NotAfter": "2024-01-15T00:00:00.0000000Z",
  "Token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "DisplayClaims": {
    "xdi": {
      "did": "{550e8400-e29b-41d4-a716-446655440000}",
      "dcs": "0"
    }
  }
}
```

The `Token` is cached as the **Device Token**.

## Step 3: Xbox Live SISU Authorization

SISU (Single Sign-In, Single Sign-Out) is Xbox Live's unified authorization system.

### 3.1 SISU Authorization Request

**Endpoint:** `POST https://sisu.xboxlive.com/authorize`

**Headers:**
```
Content-Type: application/json
x-xbl-contract-version: 1
signature: <base64-encoded signature>
```

**Request Body:**
```json
{
  "AccessToken": "t=<Microsoft Access Token>",
  "AppId": "<Application Client ID>",
  "DeviceToken": "<Device Token from Step 2>",
  "ProofKey": {
    "kty": "EC",
    "x": "<base64-url-encoded x coordinate>",
    "y": "<base64-url-encoded y coordinate>",
    "crv": "P-256",
    "alg": "ES256",
    "use": "sig"
  },
  "Sandbox": "RETAIL",
  "SiteName": "user.auth.xboxlive.com",
  "RelyingParty": "http://xboxlive.com",
  "UseModernGamertag": true
}
```

**Response:**
```json
{
  "IssueInstant": "2024-01-01T00:00:00.0000000Z",
  "NotAfter": "2024-01-02T00:00:00.0000000Z",
  "AuthorizationToken": {
    "Token": "XBL3.0 x=<user_hash>;<xsts_token>",
    "DisplayClaims": {
      "xui": [
        {
          "gtg": "GamerTag123",
          "xid": "2535405394393210",
          "uhs": "1234567890123456",
          "agg": "Adult",
          "usr": "...",
          "utr": "..."
        }
      ]
    }
  },
  "DeviceToken": "...",
  "UserToken": "..."
}
```

**Extracted Values:**
- **XSTS Token**: From `AuthorizationToken.Token`
- **Gamertag**: From `DisplayClaims.xui[0].gtg`
- **XUID**: From `DisplayClaims.xui[0].xid` (Xbox User ID)
- **User Hash**: From `DisplayClaims.xui[0].uhs`

These values form the **Xbox Identity** used for all subsequent API calls.

## Step 4: Token Storage & Refresh

### 4.1 Token Storage

All tokens are stored in:
```
~/.config/obs-studio/plugin_config/achievements-tracker/state.json
```

**File Structure:**
```json
{
  "device_uuid": "550e8400-e29b-41d4-a716-446655440000",
  "device_serial": "123456789",
  "device_private_key": "-----BEGIN EC PRIVATE KEY-----\n...\n-----END EC PRIVATE KEY-----\n",
  "microsoft_access_token": "EwAIA+pvBAAU...",
  "microsoft_refresh_token": "M.R3_BAY.-CQ...",
  "device_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "device_token_expires": "2024-01-15T00:00:00.0000000Z",
  "xsts_token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "xsts_token_expires": "2024-01-02T00:00:00.0000000Z",
  "gamertag": "GamerTag123",
  "xuid": "2535405394393210",
  "user_hash": "1234567890123456"
}
```

### 4.2 Token Refresh Logic

**On Plugin Startup:**
```
1. Load state.json
2. Check if XSTS token is still valid
   ├─ Yes → Use cached token
   └─ No → Check if refresh token exists
       ├─ Yes → Refresh Microsoft access token
       │         └─ Re-authenticate with Xbox Live (Steps 2-3)
       └─ No → Start full authentication flow (Steps 1-3)
```

**Refresh Token Exchange:**

**Endpoint:** `POST https://login.live.com/oauth20_token.srf`

**Request:**
```
client_id: <Application Client ID>
grant_type: refresh_token
refresh_token: <Cached Refresh Token>
scope: service::user.auth.xboxlive.com::MBI_SSL
```

**Response:**
```json
{
  "token_type": "bearer",
  "scope": "service::user.auth.xboxlive.com::MBI_SSL",
  "expires_in": 3600,
  "access_token": "<New Access Token>",
  "refresh_token": "<New Refresh Token>"
}
```

### 4.3 Token Expiration Handling

| Token | Typical Lifetime | Refresh Strategy |
|-------|------------------|------------------|
| **Access Token** | 1 hour | Use refresh token |
| **Refresh Token** | 90 days | Re-authenticate when expired |
| **Device Token** | 14 days | Re-authenticate with current access token |
| **XSTS Token** | 24 hours | Re-authenticate with current access token |

## Step 5: Authenticated API Calls

Once authenticated, all Xbox Live API requests include:

**Authorization Header:**
```
Authorization: XBL3.0 x=<user_hash>;<xsts_token>
```

**Example API Calls:**

**Get Profile (Gamerscore):**
```
GET https://profile.xboxlive.com/users/xuid(2535405394393210)/profile/settings?settings=Gamerscore
Authorization: XBL3.0 x=1234567890123456;eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
x-xbl-contract-version: 2
```

**Get Achievements:**
```
GET https://achievements.xboxlive.com/users/xuid(2535405394393210)/achievements
Authorization: XBL3.0 x=1234567890123456;eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
x-xbl-contract-version: 2
```

## Security Considerations

### Proof-of-Possession (PoP)

The plugin uses cryptographic signing to prove possession of the private key:
- Each authenticated request is signed with the device's private key
- Microsoft verifies the signature using the public key provided during authentication
- This prevents token theft attacks (even if tokens are intercepted, they can't be used without the private key)

### Token Storage Security

- Tokens are stored in a user-protected directory
- File permissions restrict access to the current user
- Private keys are stored in PEM format with proper encoding
- No tokens are logged or exposed in debug output

### Browser Security

- The OAuth flow uses the system default browser
- Users see the official Microsoft login page (no embedded browser)
- The plugin never sees the user's password
- Authorization is granted explicitly by the user

## Troubleshooting

### "Authorization Pending" Forever
- User hasn't completed sign-in in browser
- Check if browser window was blocked
- Verify user entered the code correctly

### "Expired Token" Error
- User took too long to sign in (>15 minutes)
- Restart authentication flow

### "Invalid Grant" on Refresh
- Refresh token expired (>90 days inactive)
- User revoked permissions
- Restart full authentication flow

### XSTS Token Issues
- Check Xbox Live service status
- Verify Microsoft account has Xbox profile
- Ensure account isn't banned/suspended

## References

- [OAuth 2.0 Device Authorization Grant](https://tools.ietf.org/html/rfc8628)
- [Microsoft Identity Platform](https://learn.microsoft.com/en-us/azure/active-directory/develop/)
- [Xbox Live Authentication](https://learn.microsoft.com/en-us/gaming/xbox-live/api-ref/xbox-live-rest/authentication/)
- [Proof-of-Possession Tokens](https://tools.ietf.org/html/rfc7800)
