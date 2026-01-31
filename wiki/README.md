# Wiki Documentation

This directory contains comprehensive documentation for the OBS Achievements Tracker Plugin.

## How to Use This Wiki

### For Users

If you want to install and use the plugin:

1. Start with **[Home.md](Home.md)** for an overview
2. Read **[What-Is-This.md](What-Is-This.md)** to understand what the plugin does
3. Follow the **[User-Guide.md](User-Guide.md)** for step-by-step installation and usage instructions

### For Developers

If you want to build, modify, or contribute to the plugin:

1. Read **[Architecture.md](Architecture.md)** to understand the technical design
2. Check **[Authentication-Flow.md](Authentication-Flow.md)** for OAuth implementation details
3. Reference **[API-Reference.md](API-Reference.md)** for Xbox Live API documentation
4. Follow the **[Development-Guide.md](Development-Guide.md)** for building and testing

## Wiki Pages

| Page | Description | Target Audience |
|------|-------------|----------------|
| [Home.md](Home.md) | Main landing page with overview and quick links | Everyone |
| [What-Is-This.md](What-Is-This.md) | Detailed description of features and use cases | Users |
| [User-Guide.md](User-Guide.md) | Installation, setup, and usage instructions | Users |
| [Architecture.md](Architecture.md) | Technical architecture and component design | Developers |
| [Authentication-Flow.md](Authentication-Flow.md) | OAuth 2.0 and Xbox Live authentication details | Developers |
| [API-Reference.md](API-Reference.md) | Xbox Live REST API documentation | Developers |
| [Development-Guide.md](Development-Guide.md) | Building, testing, and contributing | Developers |

## Using These Pages with GitHub Wiki

These markdown files are designed to work both as in-repository documentation and with GitHub's wiki feature. 

### Importing to GitHub Wiki

To use these pages as a GitHub wiki:

1. Clone the wiki repository:
   ```bash
   git clone https://github.com/Octelys/achievements-tracker-plugin.wiki.git
   ```

2. Copy the wiki files:
   ```bash
   cp -r /path/to/repo/wiki/*.md achievements-tracker-plugin.wiki/
   ```

3. Commit and push:
   ```bash
   cd achievements-tracker-plugin.wiki
   git add .
   git commit -m "Add comprehensive wiki documentation"
   git push origin master
   ```

The wiki will then be available at: https://github.com/Octelys/achievements-tracker-plugin/wiki

## Local Viewing

To view these pages locally with proper formatting:

### Option 1: GitHub Markdown Preview (VS Code)

1. Install the "Markdown Preview Enhanced" extension in VS Code
2. Open any `.md` file
3. Press `Cmd/Ctrl + Shift + V` for preview

### Option 2: Grip (GitHub Readme Instant Preview)

```bash
# Install grip
pip install grip

# Preview a file
grip wiki/Home.md
```

Then open http://localhost:6419 in your browser.

### Option 3: Markdown Reader (Browser Extension)

1. Install a Markdown reader extension for Chrome/Firefox
2. Open the `.md` files directly in your browser

## Contributing to the Wiki

To improve or expand the wiki documentation:

1. Edit the relevant `.md` file in the `wiki/` directory
2. Follow the existing formatting and structure
3. Update the table of contents if adding new sections
4. Submit a pull request with your changes

### Style Guidelines

- Use clear, concise language
- Include code examples where appropriate
- Add cross-references to related pages
- Keep tables and lists properly formatted
- Test all links before committing

## Maintenance

The wiki should be updated when:

- New features are added to the plugin
- APIs change or new endpoints are used
- Build process or dependencies change
- Common user issues are identified
- Community feedback suggests improvements

## Questions or Feedback?

- **Issues**: [GitHub Issues](https://github.com/Octelys/achievements-tracker-plugin/issues)
- **Discussions**: [GitHub Discussions](https://github.com/Octelys/achievements-tracker-plugin/discussions)
- **Pull Requests**: Contributions are welcome!
