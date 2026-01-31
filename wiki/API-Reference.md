# API Reference

This page documents the Xbox Live REST APIs used by the plugin.

## Overview

The plugin interacts with several Xbox Live REST API endpoints to fetch user data, game information, and achievements. All API requests require authentication using the XSTS token obtained through the OAuth flow.

## Authentication Header

All authenticated requests must include:

```
Authorization: XBL3.0 x=<user_hash>;<xsts_token>
x-xbl-contract-version: <version_number>
```

**Example:**
```
Authorization: XBL3.0 x=1234567890123456;eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
x-xbl-contract-version: 2
```

## Profile API

### Get User Profile Settings

Retrieve profile settings including gamerscore, gamertag, and other public information.

**Endpoint:**
```
GET https://profile.xboxlive.com/users/xuid({xuid})/profile/settings
```

**Query Parameters:**
- `settings` (required): Comma-separated list of settings to retrieve

**Available Settings:**
- `Gamerscore` - Total gamerscore
- `Gamertag` - User's gamertag
- `AccountTier` - Account type (Free/Gold)
- `XuidLowerCase` - Lowercase XUID
- `RealName` - Real name (if shared)
- `Bio` - User bio
- `Location` - Location (if shared)
- `ModernGamertag` - Modern gamertag format
- `ModernGamertagSuffix` - Numeric suffix for modern gamertags
- `UniqueModernGamertag` - Unique modern gamertag

**Example Request:**
```bash
GET https://profile.xboxlive.com/users/xuid(2535405394393210)/profile/settings?settings=Gamerscore,Gamertag
Authorization: XBL3.0 x=1234567890123456;eyJhbGci...
x-xbl-contract-version: 2
```

**Example Response:**
```json
{
  "profileUsers": [
    {
      "id": "2535405394393210",
      "hostId": null,
      "settings": [
        {
          "id": "Gamerscore",
          "value": "42500"
        },
        {
          "id": "Gamertag",
          "value": "GamerTag123"
        }
      ],
      "isSponsoredUser": false
    }
  ]
}
```

## Achievements API

### List User Achievements

Get a list of achievements for a specific user and title.

**Endpoint:**
```
GET https://achievements.xboxlive.com/users/xuid({xuid})/achievements
```

**Query Parameters:**
- `titleId` (optional): Xbox title ID to filter achievements
- `orderBy` (optional): Sort order (`Unordered`, `TitleId`, `UnlockTime`)
- `maxItems` (optional): Maximum number of items to return (default: 10, max: 100)
- `skipItems` (optional): Number of items to skip for pagination
- `continuationToken` (optional): Token for pagination

**Example Request:**
```bash
GET https://achievements.xboxlive.com/users/xuid(2535405394393210)/achievements?titleId=1234567890&maxItems=25
Authorization: XBL3.0 x=1234567890123456;eyJhbGci...
x-xbl-contract-version: 2
```

**Example Response:**
```json
{
  "achievements": [
    {
      "id": "1",
      "serviceConfigId": "12345678-1234-1234-1234-123456789012",
      "name": "Achievement Name",
      "titleAssociations": [
        {
          "name": "Game Title",
          "id": 1234567890
        }
      ],
      "progressState": "Achieved",
      "progression": {
        "requirements": [
          {
            "id": "1",
            "current": "1",
            "target": "1"
          }
        ],
        "timeUnlocked": "2024-01-01T12:00:00.0000000Z"
      },
      "mediaAssets": [
        {
          "name": "Icon",
          "type": "Icon",
          "url": "https://..."
        }
      ],
      "platform": "XboxOne",
      "isSecret": false,
      "description": "Achievement description",
      "lockedDescription": "Locked description",
      "productId": "...",
      "achievementType": "Challenge",
      "participationType": "Individual",
      "timeWindow": null,
      "rewards": [
        {
          "name": null,
          "description": null,
          "value": "10",
          "type": "Gamerscore",
          "valueType": "Int"
        }
      ],
      "estimatedTime": "00:05:00",
      "deeplink": "...",
      "isRevoked": false
    }
  ],
  "pagingInfo": {
    "continuationToken": "...",
    "totalRecords": 50
  }
}
```

### Get Title Achievements

Get all achievements for a specific title.

**Endpoint:**
```
GET https://achievements.xboxlive.com/users/xuid({xuid})/achievements/title/{titleId}
```

**Example Request:**
```bash
GET https://achievements.xboxlive.com/users/xuid(2535405394393210)/achievements/title/1234567890
Authorization: XBL3.0 x=1234567890123456;eyJhbGci...
x-xbl-contract-version: 2
```

## Presence API

### Get User Presence

Determine what game or app the user is currently using.

**Endpoint:**
```
GET https://userpresence.xboxlive.com/users/xuid({xuid})
```

**Query Parameters:**
- `level` (optional): Detail level (`user`, `device`, `title`, `all`)

**Example Request:**
```bash
GET https://userpresence.xboxlive.com/users/xuid(2535405394393210)?level=all
Authorization: XBL3.0 x=1234567890123456;eyJhbGci...
x-xbl-contract-version: 3
```

**Example Response:**
```json
{
  "xuid": "2535405394393210",
  "state": "Online",
  "devices": [
    {
      "type": "XboxOne",
      "titles": [
        {
          "id": "1234567890",
          "name": "Game Title",
          "state": "Active",
          "placement": "Full",
          "timestamp": "2024-01-01T12:00:00.0000000Z",
          "activity": {
            "richPresence": "Playing multiplayer"
          }
        }
      ]
    }
  ]
}
```

### Batch User Presence

Get presence for multiple users at once.

**Endpoint:**
```
POST https://userpresence.xboxlive.com/users/batch
```

**Request Body:**
```json
{
  "users": [
    "2535405394393210",
    "1234567890123456"
  ],
  "level": "all"
}
```

## People API

### Get Friends List

Retrieve the user's friends list.

**Endpoint:**
```
GET https://social.xboxlive.com/users/xuid({xuid})/people
```

**Query Parameters:**
- `view` (optional): View type (`all`, `favorite`, `mutualfollowing`)

**Example Request:**
```bash
GET https://social.xboxlive.com/users/xuid(2535405394393210)/people
Authorization: XBL3.0 x=1234567890123456;eyJhbGci...
x-xbl-contract-version: 1
```

## Title Hub API

### Get Title Details

Get detailed information about a game title.

**Endpoint:**
```
GET https://titlehub.xboxlive.com/titles/{titleId}/decoration/detail
```

**Example Request:**
```bash
GET https://titlehub.xboxlive.com/titles/1234567890/decoration/detail
Authorization: XBL3.0 x=1234567890123456;eyJhbGci...
x-xbl-contract-version: 2
```

**Example Response:**
```json
{
  "titles": [
    {
      "titleId": "1234567890",
      "pfn": "...",
      "bingId": "...",
      "windowsPhoneProductId": null,
      "name": "Game Title",
      "type": "Game",
      "devices": ["XboxOne", "Scarlett", "PC"],
      "displayImage": "https://...",
      "mediaItemType": "Game",
      "modernTitleId": "...",
      "isBundle": false,
      "achievement": {
        "currentAchievements": 15,
        "totalAchievements": 50,
        "currentGamerscore": 250,
        "totalGamerscore": 1000
      },
      "stats": {
        "numOfPlayers": 1000000
      },
      "gamePass": {
        "isGamePass": true
      }
    }
  ]
}
```

## Media API (Cover Art)

### Get Game Cover Art

Game cover art and media assets are referenced in the title hub response.

**Common Image Types:**
- **Box Art**: `https://titlehub.xboxlive.com/titles/{titleId}/images/boxart`
- **Screenshots**: `https://titlehub.xboxlive.com/titles/{titleId}/images/screenshot`
- **Hero Art**: `https://titlehub.xboxlive.com/titles/{titleId}/images/hero`

**Dynamic Image Service:**
```
https://images-eds.xboxlive.com/image?url={encoded_image_url}&w={width}&h={height}&format={format}
```

**Parameters:**
- `url`: Base64-encoded image URL
- `w`: Desired width
- `h`: Desired height
- `format`: Image format (`png`, `jpg`)

**Example:**
```
https://images-eds.xboxlive.com/image?url=abc123&w=300&h=300&format=png
```

## Gamerpic API

### Get User Gamerpic

Retrieve the user's profile picture.

**Endpoint:**
```
GET https://profile.xboxlive.com/users/xuid({xuid})/profile/settings?settings=GameDisplayPicRaw
```

**Example Response:**
```json
{
  "profileUsers": [
    {
      "id": "2535405394393210",
      "settings": [
        {
          "id": "GameDisplayPicRaw",
          "value": "https://images-eds-ssl.xboxlive.com/..."
        }
      ]
    }
  ]
}
```

## Real-Time Activity (RTA) WebSocket

The plugin can optionally connect to Xbox Live's Real-Time Activity service for push notifications.

**Endpoint:**
```
wss://rta.xboxlive.com/connect
```

**Connection:**
1. Establish WebSocket connection
2. Send subscribe message for desired events
3. Receive push notifications when events occur

**Subscribe Message:**
```json
{
  "id": 1,
  "command": "subscribe",
  "path": "/users/xuid({xuid})/presence"
}
```

**Event Notification:**
```json
{
  "id": 0,
  "data": {
    "xuid": "2535405394393210",
    "state": "Online",
    "devices": [...]
  }
}
```

## Error Handling

### Common Error Codes

| Status Code | Meaning | Typical Cause |
|-------------|---------|---------------|
| `400` | Bad Request | Invalid parameters or malformed request |
| `401` | Unauthorized | Invalid or expired XSTS token |
| `403` | Forbidden | Insufficient permissions or banned account |
| `404` | Not Found | Resource doesn't exist (e.g., invalid XUID) |
| `429` | Too Many Requests | Rate limit exceeded |
| `500` | Internal Server Error | Xbox Live service issue |
| `503` | Service Unavailable | Temporary outage |

### Error Response Format

```json
{
  "code": "BadRequest",
  "message": "Invalid parameter: settings",
  "details": {
    "parameter": "settings",
    "reason": "Must be comma-separated list"
  }
}
```

## Rate Limits

Xbox Live APIs enforce rate limits to prevent abuse:

- **Profile API**: ~300 requests/15 minutes
- **Achievements API**: ~100 requests/15 minutes
- **Presence API**: ~300 requests/15 minutes

**Best Practices:**
- Cache responses when possible
- Use batch endpoints to reduce request count
- Implement exponential backoff on 429 errors
- Monitor `X-RateLimit-*` headers (if provided)

## Data Caching

The plugin caches API responses to minimize requests:

| Data Type | Cache Duration | Refresh Trigger |
|-----------|----------------|-----------------|
| **Gamerscore** | 5 minutes | Manual refresh or achievement unlock |
| **Game Cover** | Session | Game change detected |
| **Achievements List** | 10 minutes | Manual refresh |
| **Presence** | 30 seconds | Continuous polling |
| **Profile Settings** | 15 minutes | Manual refresh |

## Testing APIs

For testing without the plugin, you can use `curl`:

```bash
# Get gamerscore
curl -X GET \
  "https://profile.xboxlive.com/users/xuid(YOUR_XUID)/profile/settings?settings=Gamerscore" \
  -H "Authorization: XBL3.0 x=YOUR_UHS;YOUR_XSTS_TOKEN" \
  -H "x-xbl-contract-version: 2"

# Get achievements
curl -X GET \
  "https://achievements.xboxlive.com/users/xuid(YOUR_XUID)/achievements?maxItems=10" \
  -H "Authorization: XBL3.0 x=YOUR_UHS;YOUR_XSTS_TOKEN" \
  -H "x-xbl-contract-version: 2"
```

## References

- [Xbox Live REST API Documentation](https://learn.microsoft.com/en-us/gaming/xbox-live/api-ref/xbox-live-rest/)
- [Xbox Live API Deep Wiki](https://deepwiki.com/microsoft/xbox-live-api/)
- [Gamerpic API Reference](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/live/rest/uri/gamerpic/atoc-reference-gamerpic)
- [Real-Time Activity System](https://deepwiki.com/microsoft/xbox-live-api/5-real-time-activity-system)
