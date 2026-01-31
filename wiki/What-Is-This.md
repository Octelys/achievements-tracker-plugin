# What Is This?

The **OBS Achievements Tracker Plugin** is a cross-platform plugin for OBS Studio that integrates Xbox Live data directly into your streaming setup. It allows streamers to display real-time Xbox gaming information, including achievements, gamer scores, and game cover art.

## Overview

This plugin provides a seamless way to showcase your Xbox gaming progress during live streams or recordings. By authenticating with your Microsoft account, the plugin fetches and displays your Xbox Live data in real-time.

## Key Features

### 🎮 Xbox Live Integration
- **Secure Authentication**: OAuth 2.0 authentication flow with Microsoft accounts
- **Real-time Data**: Automatically updates as you play games and unlock achievements
- **Privacy-Focused**: All authentication tokens are stored locally and encrypted

### 📊 Data Sources

The plugin provides three OBS sources that you can add to your scenes:

#### 1. Xbox Account
- Sign in with your Microsoft account
- Manage authentication and authorization
- View connection status

#### 2. Xbox Game Cover
- Automatically displays the cover art of the game you're currently playing
- Updates in real-time when you switch games
- High-quality game artwork from Xbox Live

#### 3. Xbox Gamerscore
- Shows your total Xbox gamerscore
- Updates automatically as you earn achievements
- Customizable display format (coming soon)

### 🔄 Real-Time Updates
- Monitors your Xbox Live activity
- Detects when you start playing a new game
- Tracks achievement unlocks as they happen
- Automatically updates all sources

### 🌐 Cross-Platform Support
- **Windows**: Native support with full functionality
- **macOS**: Universal binary supporting Intel and Apple Silicon
- **Linux**: Fully compatible with various distributions

## Use Cases

### For Streamers
- Display your gamerscore during achievement hunting streams
- Show game cover art to let viewers know what you're playing
- Track achievement progress in real-time
- Create achievement unlock overlays (future feature)

### For Content Creators
- Record gameplay with achievement tracking
- Create highlight reels of achievement unlocks
- Document your gaming journey with accurate metrics

### For Competitive Players
- Showcase your high gamerscore
- Demonstrate completion rates
- Track progress across multiple games

## How It Works

1. **Authentication**: You sign in once with your Microsoft account through a secure OAuth 2.0 flow
2. **Token Management**: The plugin securely stores authentication tokens locally
3. **API Communication**: The plugin communicates with Xbox Live APIs to fetch your data
4. **Real-Time Monitoring**: A background service monitors your Xbox activity for changes
5. **OBS Integration**: Data is displayed through native OBS sources that you can style and position

## Privacy & Security

- **No Third-Party Servers**: All communication is directly between your computer and Microsoft's servers
- **Local Token Storage**: Authentication tokens are stored securely on your local machine
- **Open Source**: The entire codebase is available for review on GitHub
- **Standard OAuth 2.0**: Uses Microsoft's official authentication flow

## What's Next?

The plugin is actively developed with planned features including:
- Achievement unlock notifications
- Customizable text formatting and styling
- Achievement progress tracking
- Support for multiple Xbox accounts
- Enhanced visual customization options

## Limitations

- Requires an active Xbox Live account
- Internet connection needed for real-time updates
- Some features depend on Xbox Live API availability
- Currently tracks only the signed-in user's data

## Getting Started

Ready to add Xbox Live tracking to your streams? Check out the [User Guide](User-Guide.md) for installation and setup instructions.
