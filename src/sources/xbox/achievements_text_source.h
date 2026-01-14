#pragma once

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

void xbox_achievements_text_source_register(void);

const struct obs_source_info *xbox_achievements_text_source_get(void);

/**
 * Sets the image URL to display in the source.
 * The image will be downloaded and rendered on the next frame.
 * Pass NULL or empty string to clear the image.
 */
void xbox_achievements_text_source_set_image_url(const char *url);

#ifdef __cplusplus
}
#endif
