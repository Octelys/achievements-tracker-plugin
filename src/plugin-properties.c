#include "plugin-properties.h"

#include <graphics/graphics.h>

#include "plugin-text-source.h"

/*
 * Forward declarations for the source callbacks.
 *
 * They’re implemented in plugin-text-source.c, but we keep the obs_source_info
 * structure itself isolated in this file.
 */
const char *text_src_get_name(void *unused);
obs_properties_t *get_properties(void *data);
void *text_src_create(obs_data_t *settings, obs_source_t *source);
void text_src_destroy(void *data);
void text_src_update(void *data, obs_data_t *settings);
uint32_t text_src_get_width(void *data);
uint32_t text_src_get_height(void *data);
void text_src_video_render(void *data, gs_effect_t *effect);

static struct obs_source_info text_src_info = {
	.id = "template_text_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = text_src_get_name,
	.create = text_src_create,
	.destroy = text_src_destroy,
	.update = text_src_update,
	.get_properties = get_properties,
	.get_width = text_src_get_width,
	.get_height = text_src_get_height,
	.video_tick = NULL,
	.video_render = text_src_video_render,
};

const struct obs_source_info *get_plugin_properties(void)
{
	return &text_src_info;
}
