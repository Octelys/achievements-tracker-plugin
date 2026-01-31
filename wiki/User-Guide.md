# User Guide

A complete guide for installing and using the OBS Achievements Tracker Plugin.

## Table of Contents

- [Installation](#installation)
- [Initial Setup](#initial-setup)
- [Adding Sources to Your Scene](#adding-sources-to-your-scene)
- [Source Types](#source-types)
- [Configuration](#configuration)
- [Troubleshooting](#troubleshooting)
- [FAQ](#faq)

## Installation

### Prerequisites

- **OBS Studio 30.0 or later** installed on your system
- **Xbox Live Account** (Microsoft account with Xbox profile)
- **Internet Connection** for authentication and real-time updates

### Download

1. Go to the [Releases](https://github.com/Octelys/achievements-tracker-plugin/releases) page
2. Download the latest release for your platform:
   - **Windows:** `achievements-tracker-windows-x64.zip`
   - **macOS:** `achievements-tracker-macos-universal.zip`
   - **Linux:** `achievements-tracker-linux-x64.tar.gz`

### Platform-Specific Installation

#### Windows

1. **Extract the Archive:**
   - Right-click the downloaded `.zip` file
   - Select "Extract All..."
   - Choose a temporary location

2. **Copy to OBS Plugins Directory:**
   - Navigate to: `C:\Program Files\obs-studio\obs-plugins\64bit\`
   - Copy the contents of the extracted folder into this directory

3. **Restart OBS Studio**

#### macOS

1. **Extract the Archive:**
   - Double-click the downloaded `.zip` file
   - This will create a `.plugin` bundle

2. **Install the Plugin:**
   - Open Finder
   - Press `Cmd + Shift + G` (Go to Folder)
   - Enter: `~/Library/Application Support/obs-studio/plugins/`
   - If the folder doesn't exist, create it
   - Drag the `achievements-tracker.plugin` into this folder

3. **Restart OBS Studio**

#### Linux

1. **Extract the Archive:**
   ```bash
   tar -xzf achievements-tracker-linux-x64.tar.gz
   ```

2. **Install to OBS Plugins Directory:**
   ```bash
   mkdir -p ~/.config/obs-studio/plugins
   cp achievements-tracker.so ~/.config/obs-studio/plugins/
   ```

3. **Restart OBS Studio**

## Initial Setup

### Signing In to Xbox Live

Before you can use any Xbox sources, you need to authenticate with your Microsoft account.

1. **Open OBS Studio**

2. **Add Xbox Account Source:**
   - In the **Sources** panel, click the **+** button
   - Select **Xbox Account** from the list
   - Give it a name (e.g., "My Xbox Account")
   - Click **OK**

3. **Sign In:**
   - In the properties window, click **Sign in with Xbox Live**
   - A browser window will open to Microsoft's login page
   - Sign in with your Microsoft account credentials
   - You'll see a device code displayed (e.g., "ABCD1234")
   - Authorize the application

4. **Wait for Authentication:**
   - Return to OBS
   - The plugin will automatically detect when you've signed in
   - You'll see your **Gamertag** displayed in the properties
   - Click **OK** to close the properties window

**Note:** You only need to sign in once. The plugin will remember your authentication and automatically refresh tokens when needed.

## Adding Sources to Your Scene

Once you're signed in, you can add Xbox data sources to your scenes.

### Xbox Game Cover

Displays the cover art of the game you're currently playing on Xbox.

1. Click the **+** button in the **Sources** panel
2. Select **Xbox Game Cover**
3. Name the source (e.g., "Current Game Cover")
4. Click **OK**
5. Position and resize the source in your scene

The cover will automatically update when you start playing a different game.

### Xbox Gamerscore

Displays your total Xbox gamerscore.

1. Click the **+** button in the **Sources** panel
2. Select **Xbox Gamerscore**
3. Name the source (e.g., "My Gamerscore")
4. Click **OK**
5. Position the text in your scene

The gamerscore will automatically update as you earn achievements.

## Source Types

### Xbox Account

**Purpose:** Authentication and account management

**Features:**
- Sign in/sign out functionality
- Displays connection status
- Shows your gamertag when signed in

**Properties:**
- **Sign in with Xbox Live:** Opens browser for authentication
- **Sign Out:** Clears stored credentials
- **Status:** Shows connection status (Connected/Disconnected)
- **Gamertag:** Your Xbox gamertag (read-only)

### Xbox Game Cover

**Purpose:** Display cover art for your currently playing game

**Features:**
- Automatically fetches and displays game cover art
- Updates in real-time when you switch games
- High-quality artwork from Xbox Live

**Properties:**
- **Refresh:** Manually refresh the current game
- **Width/Height:** Resize the cover art
- **Opacity:** Adjust transparency (0-100%)

**Tips:**
- Position this source prominently in your scene
- Works great with a border or shadow effect
- Automatically hides when no game is being played

### Xbox Gamerscore

**Purpose:** Display your total Xbox gamerscore

**Features:**
- Shows current gamerscore
- Updates automatically on achievement unlocks
- Customizable text display (future feature)

**Properties:**
- **Refresh:** Manually refresh gamerscore
- **Font:** Choose display font (future feature)
- **Color:** Set text color (future feature)
- **Size:** Adjust text size (future feature)

**Tips:**
- Use with a semi-transparent background for better visibility
- Position near achievement notifications for context

## Configuration

### Managing Authentication

#### Viewing Stored Data

Authentication tokens are stored in:
- **macOS:** `~/Library/Application Support/obs-studio/plugin_config/achievements-tracker/state.json`
- **Windows:** `%APPDATA%\obs-studio\plugin_config\achievements-tracker\state.json`
- **Linux:** `~/.config/obs-studio/plugin_config/achievements-tracker/state.json`

#### Signing Out

To remove stored credentials:

1. Open Xbox Account source properties
2. Click **Sign Out**
3. Confirm the action

This will delete all stored tokens and require re-authentication.

#### Re-authenticating

If your session expires or you want to switch accounts:

1. Sign out of the current account
2. Click **Sign in with Xbox Live** again
3. Complete the authentication flow

### Performance Settings

The plugin automatically optimizes data fetching:
- **Game Cover:** Fetched once per game change
- **Gamerscore:** Updated every 5 minutes or on manual refresh
- **Presence:** Checked every 30 seconds

These settings are optimized for performance and cannot be changed at this time.

## Troubleshooting

### Common Issues

#### "Failed to Authenticate"

**Symptoms:** Authentication window opens but sign-in fails

**Solutions:**
1. Check your internet connection
2. Ensure you're entering the correct device code
3. Try signing in again after a few minutes
4. Check if Xbox Live service is available
5. Verify your Microsoft account has an Xbox profile

#### "No Game Cover Displayed"

**Symptoms:** Game cover source is empty or shows placeholder

**Solutions:**
1. Ensure you're currently playing a game on Xbox
2. Check that your Xbox profile is set to "Online"
3. Verify the game is an Xbox title (not a PC-only game)
4. Try manually refreshing the source
5. Check your Xbox Live connection status

#### "Gamerscore Not Updating"

**Symptoms:** Gamerscore doesn't change after earning achievements

**Solutions:**
1. Wait a few minutes (updates can be delayed)
2. Manually refresh the gamerscore source
3. Check if you're signed in to Xbox Live
4. Verify achievements actually unlocked on Xbox Live website
5. Restart OBS Studio

#### "Token Expired" Error

**Symptoms:** Sources show disconnected or error state

**Solutions:**
1. Open Xbox Account source properties
2. The plugin should automatically refresh tokens
3. If it doesn't, sign out and sign in again
4. Check the OBS log file for detailed error messages

#### Plugin Doesn't Appear in OBS

**Symptoms:** Xbox sources not listed in OBS

**Solutions:**
1. Verify the plugin is installed in the correct directory
2. Check OBS version (requires 30.0+)
3. Restart OBS Studio
4. Check OBS log file for plugin loading errors
5. Ensure all dependencies are installed (OpenSSL, libcurl)

### Log Files

OBS log files can help diagnose issues:

**Location:**
- **macOS:** `~/Library/Application Support/obs-studio/logs/`
- **Windows:** `%APPDATA%\obs-studio\logs\`
- **Linux:** `~/.config/obs-studio/logs/`

**Accessing Logs:**
1. In OBS, go to **Help → Log Files**
2. Select **Upload Current Log** or **Show Log Files**
3. Search for "achievements-tracker" to find relevant messages

### Getting Help

If you're still experiencing issues:

1. **Check GitHub Issues:** [github.com/Octelys/achievements-tracker-plugin/issues](https://github.com/Octelys/achievements-tracker-plugin/issues)
2. **Open a New Issue:** Include:
   - OBS version
   - Plugin version
   - Operating system
   - Steps to reproduce
   - Relevant log excerpts
3. **Join Discussions:** [github.com/Octelys/achievements-tracker-plugin/discussions](https://github.com/Octelys/achievements-tracker-plugin/discussions)

## FAQ

### General Questions

**Q: Does this plugin work with PlayStation or Nintendo accounts?**  
A: No, this plugin is specifically designed for Xbox Live. PlayStation and Nintendo support is not planned.

**Q: Can I use this offline?**  
A: No, the plugin requires an internet connection to authenticate and fetch data from Xbox Live.

**Q: Does this affect my game performance?**  
A: No, the plugin only communicates with Xbox Live servers. It doesn't interact with your games directly.

**Q: Can I track multiple Xbox accounts?**  
A: Not currently. This feature is planned for a future release.

**Q: Is my data secure?**  
A: Yes. All authentication is done through Microsoft's official OAuth 2.0 flow. Tokens are stored locally on your computer, and the plugin never sends your data to third-party servers.

### Features

**Q: Can I customize the appearance of the gamerscore text?**  
A: This feature is planned but not yet available. Currently, you can use OBS's text source styling options.

**Q: Can I get notifications when I unlock achievements?**  
A: This feature is planned for a future release.

**Q: Can I display achievement progress (e.g., "15/50 achievements")?**  
A: This feature is in development and will be available in a future update.

**Q: Does this work with Xbox Cloud Gaming (xCloud)?**  
A: Yes, as long as the game is recognized by Xbox Live and you're signed in with your account.

### Technical Questions

**Q: Why does the plugin need browser access?**  
A: The OAuth 2.0 authentication flow requires opening Microsoft's login page in a browser. This is the standard and most secure way to authenticate.

**Q: How often does the plugin check for updates?**  
A: Game presence is checked every 30 seconds, gamerscore every 5 minutes. These intervals are optimized to balance real-time updates with API rate limits.

**Q: Can I use this plugin on a streaming PC while gaming on a different PC?**  
A: Yes, the plugin fetches data from Xbox Live servers, not your gaming PC. As long as you're playing games on Xbox Live (console or PC), the plugin will work.

**Q: Does this work with Game Pass games?**  
A: Yes, all games available through Xbox Live, including Game Pass titles, are supported.

### Privacy

**Q: What data does the plugin collect?**  
A: The plugin only accesses data you explicitly authorize during sign-in: your gamertag, gamerscore, achievement data, and currently playing game. No data is sent to third parties.

**Q: Can I delete my data?**  
A: Yes, use the "Sign Out" button in the Xbox Account source properties. This deletes all locally stored tokens and credentials.

**Q: Is the plugin open source?**  
A: Yes, the entire source code is available on GitHub for review and contribution.

## Tips and Best Practices

### Stream Overlay Design

- **Position game cover prominently:** Make it easy for viewers to see what you're playing
- **Add a background to gamerscore:** Improve readability with a semi-transparent background
- **Use consistent styling:** Match colors and fonts with your overall stream theme
- **Test before going live:** Ensure all sources are updating correctly

### Performance Optimization

- **Don't add duplicate sources:** Multiple instances of the same source don't improve update speed
- **Use manual refresh sparingly:** Automatic updates are optimized; manual refreshes can hit rate limits
- **Monitor OBS CPU usage:** The plugin is lightweight but adding too many sources can impact performance

### Privacy Considerations

- **Be aware of what's visible:** Your gamertag and gamerscore are public on your stream
- **Consider offline mode:** Hide Xbox sources when not actively streaming gaming content
- **Review privacy settings:** Check your Xbox Live privacy settings if you want to limit what's shared

## Updating the Plugin

### Checking for Updates

1. Visit the [Releases](https://github.com/Octelys/achievements-tracker-plugin/releases) page
2. Compare your installed version with the latest release
3. Download and install the newer version following the installation instructions

### Update Process

1. **Close OBS Studio**
2. **Backup your state file** (optional, but recommended):
   - Copy `state.json` to a safe location
3. **Install the new version** over the old one
4. **Restart OBS Studio**
5. **Verify functionality** by checking that sources still work

**Note:** Updates typically preserve your authentication. If you need to re-authenticate, your credentials are safe; the plugin just needs fresh tokens.

## Uninstalling

To remove the plugin:

1. **Close OBS Studio**
2. **Delete the plugin files:**
   - **Windows:** Delete files from `C:\Program Files\obs-studio\obs-plugins\64bit\`
   - **macOS:** Delete `achievements-tracker.plugin` from `~/Library/Application Support/obs-studio/plugins/`
   - **Linux:** Delete `achievements-tracker.so` from `~/.config/obs-studio/plugins/`
3. **Delete saved data (optional):**
   - Delete the `state.json` file from the plugin config directory (see [Configuration](#configuration) section)
4. **Restart OBS Studio**

## Additional Resources

- [What Is This?](What-Is-This.md) - Learn more about the plugin
- [Architecture](Architecture.md) - Technical details for advanced users
- [Development Guide](Development-Guide.md) - Build the plugin from source
- [GitHub Repository](https://github.com/Octelys/achievements-tracker-plugin) - Source code and issue tracking
