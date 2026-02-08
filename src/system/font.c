#include "font.h"

#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <CoreText/CoreText.h>
#endif

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

/**
 * @brief Comparison function for qsort to sort fonts alphabetically by name.
 */
static int compare_fonts_by_name(const void *a, const void *b) {
    const font_t *fa = (const font_t *)a;
    const font_t *fb = (const font_t *)b;

    if (!fa->name && !fb->name) {
        return 0;
    }

    if (!fa->name) {
        return 1;
    }

    if (!fb->name) {
        return -1;
    }

    return strcasecmp(fa->name, fb->name);
}

font_t *font_list_available(size_t *out_count) {

    if (!out_count) {
        return NULL;
    }

    *out_count = 0;

#ifdef __APPLE__
    // Create a font collection containing all available fonts.
    CTFontCollectionRef collection = CTFontCollectionCreateFromAvailableFonts(NULL);
    if (!collection) {
        return NULL;
    }

    CFArrayRef descriptors = CTFontCollectionCreateMatchingFontDescriptors(collection);
    CFRelease(collection);

    if (!descriptors) {
        return NULL;
    }

    CFIndex count = CFArrayGetCount(descriptors);
    if (count <= 0) {
        CFRelease(descriptors);
        return NULL;
    }

    font_t *fonts = calloc((size_t)count, sizeof(font_t));
    if (!fonts) {
        CFRelease(descriptors);
        return NULL;
    }

    size_t valid_count = 0;

    for (CFIndex i = 0; i < count; i++) {
        CTFontDescriptorRef desc = (CTFontDescriptorRef)CFArrayGetValueAtIndex(descriptors, i);
        if (!desc) {
            continue;
        }

        // Get font URL (path).
        CFURLRef url = (CFURLRef)CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute);
        if (!url) {
            continue;
        }

        char path_buf[1024];
        if (!CFURLGetFileSystemRepresentation(url, true, (UInt8 *)path_buf, sizeof(path_buf))) {
            CFRelease(url);
            continue;
        }

        // Get font display name.
        CFStringRef display_name  = (CFStringRef)CTFontDescriptorCopyAttribute(desc, kCTFontDisplayNameAttribute);
        char        name_buf[256] = {0};

        if (display_name) {
            CFStringGetCString(display_name, name_buf, sizeof(name_buf), kCFStringEncodingUTF8);
            CFRelease(display_name);
        }

        // If no display name, extract from path.
        if (name_buf[0] == '\0') {
            const char *last_slash = strrchr(path_buf, '/');
            if (last_slash) {
                strncpy(name_buf, last_slash + 1, sizeof(name_buf) - 1);
            } else {
                strncpy(name_buf, path_buf, sizeof(name_buf) - 1);
            }
        }

        fonts[valid_count].path = strdup(path_buf);
        fonts[valid_count].name = strdup(name_buf);

        if (fonts[valid_count].path && fonts[valid_count].name) {
            valid_count++;
        } else {
            // Clean up partial allocation.
            free(fonts[valid_count].path);
            free(fonts[valid_count].name);
            fonts[valid_count].path = NULL;
            fonts[valid_count].name = NULL;
        }

        CFRelease(url);
    }

    CFRelease(descriptors);

    // Sort alphabetically by name.
    if (valid_count > 1) {
        qsort(fonts, valid_count, sizeof(font_t), compare_fonts_by_name);
    }

    *out_count = valid_count;
    return fonts;

#elif defined(_WIN32)
    // TODO: Windows implementation using GetFonts or enumerating C:\Windows\Fonts.
    return NULL;

#elif defined(__linux__)
    // TODO: Linux implementation using fontconfig.
    return NULL;

#else
    return NULL;
#endif
}

void font_list_free(font_t *fonts, size_t count) {

    if (!fonts) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        free(fonts[i].name);
        free(fonts[i].path);
    }

    free(fonts);
}
