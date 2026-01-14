# Vendored cJSON

This folder contains a vendored copy of the `cJSON` library and the optional `cJSON_Utils` helpers.

Why vendored?
- This plugin builds with CMake across multiple platforms.
- Keeping JSON parsing as an in-tree dependency avoids system package differences.

## Updating
Replace the files in this directory with the upstream versions from the cJSON project:
- `cJSON.c`, `cJSON.h`
- `cJSON_Utils.c`, `cJSON_Utils.h`

Then rebuild.

## Notes
The current vendored `cJSON.c`/`cJSON_Utils.c` are intentionally minimal implementations to satisfy compilation and linking needs.
If you start using more advanced features, update with the full upstream sources.

