#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Font descriptor containing both display name and file path.
 */
typedef struct font {
    char *name; /**< Display name of the font (e.g., "Helvetica Bold"). */
    char *path; /**< Full file path to the font file. */
} font_t;

/**
 * @brief Get a list of all available fonts on the system.
 *
 * On macOS, this uses CoreText to enumerate fonts.
 * On Linux, this will use fontconfig.
 * On Windows, this will enumerate the Fonts directory.
 *
 * The returned list is sorted alphabetically by font name.
 *
 * @param out_count Pointer to receive the number of fonts found.
 * @return Array of font_t structures (caller must free with font_list_free).
 *         Returns NULL on failure.
 */
font_t *font_list_available(size_t *out_count);

/**
 * @brief Free a font list returned by font_list_available.
 *
 * @param fonts Array of font_t structures.
 * @param count Number of elements in the array.
 */
void font_list_free(font_t *fonts, size_t count);

#ifdef __cplusplus
}
#endif
