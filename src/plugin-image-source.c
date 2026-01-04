#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/image-file.h>

#include <plugin-support.h>

#include "plugin-image-source.h"

struct image_src {
	obs_source_t *source;
	char *path;

	gs_image_file_t image;
	bool loaded;
	bool texture_ready;
};

static const char *image_src_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "My plugin PNG image";
}

static obs_properties_t *image_src_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();

	/* A file picker that filters on .png */
	obs_properties_add_path(p, "path", "PNG file", OBS_PATH_FILE,
				"PNG (*.png)", NULL);

	return p;
}

static void image_src_unload(struct image_src *s)
{
	if (!s)
		return;

	if (s->loaded || s->texture_ready) {
		gs_image_file_free(&s->image);
		memset(&s->image, 0, sizeof(s->image));
		s->loaded = false;
		s->texture_ready = false;
	}
}

static void image_src_load(struct image_src *s)
{
	if (!s)
		return;

	image_src_unload(s);

	if (!s->path || !*s->path) {
		obs_log(LOG_INFO, "[my-plugin] PNG image source: no path set");
		return;
	}

	/* Decode image on CPU now; create GPU texture later on the render thread. */
	gs_image_file_init(&s->image, s->path);

	if (s->image.loaded) {
		s->loaded = true;
		s->texture_ready = false;
		obs_log(LOG_INFO,
			"[my-plugin] Loaded image file: %s (%ux%u)",
			s->path, s->image.cx, s->image.cy);
	} else {
		obs_log(LOG_WARNING, "[my-plugin] Failed to load image: %s", s->path);
	}
}

static void *image_src_create(obs_data_t *settings, obs_source_t *source)
{
	struct image_src *s = bzalloc(sizeof(*s));
	s->source = source;

	const char *p = obs_data_get_string(settings, "path");
	s->path = bstrdup(p ? p : "");

	image_src_load(s);
	return s;
}

static void image_src_destroy(void *data)
{
	struct image_src *s = data;
	if (!s)
		return;

	image_src_unload(s);
	bfree(s->path);
	bfree(s);
}

static void image_src_update(void *data, obs_data_t *settings)
{
	struct image_src *s = data;
	const char *p = obs_data_get_string(settings, "path");

	bfree(s->path);
	s->path = bstrdup(p ? p : "");

	image_src_load(s);
}

static uint32_t image_src_get_width(void *data)
{
	struct image_src *s = data;
	if (!s || !s->loaded)
		return 0;
	return s->image.cx;
}

static uint32_t image_src_get_height(void *data)
{
	struct image_src *s = data;
	if (!s || !s->loaded)
		return 0;
	return s->image.cy;
}

static void image_src_video_render(void *data, gs_effect_t *effect)
{
	struct image_src *s = data;
	if (!s || !s->loaded)
		return;

	/* Must be done on the graphics thread */
	if (!s->texture_ready) {
		gs_image_file_init_texture(&s->image);
		if (!s->image.texture) {
			obs_log(LOG_WARNING,
				"[my-plugin] Image texture creation failed for: %s",
				s->path ? s->path : "(null)");
			return;
		}
		s->texture_ready = true;
	}

	/*
	 * OBS may already have an effect active when it calls video_render.
	 * Starting a new gs_effect_loop in that case triggers:
	 *   gs_effect_loop: An effect is already active
	 */
	gs_effect_t *e = effect ? effect : obs_get_base_effect(OBS_EFFECT_DEFAULT);
	if (!e)
		return;

	gs_eparam_t *image = gs_effect_get_param_by_name(e, "image");
	if (image)
		gs_effect_set_texture(image, s->image.texture);

	if (effect) {
		/* Effect is already active; just draw. */
		gs_draw_sprite(s->image.texture, 0, s->image.cx, s->image.cy);
	} else {
		while (gs_effect_loop(e, "Draw")) {
			gs_draw_sprite(s->image.texture, 0, s->image.cx, s->image.cy);
		}
	}
}

static struct obs_source_info image_src_info = {
	.id = "my_plugin_png_image",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = image_src_get_name,
	.create = image_src_create,
	.destroy = image_src_destroy,
	.update = image_src_update,
	.get_properties = image_src_properties,
	.get_width = image_src_get_width,
	.get_height = image_src_get_height,
	.video_render = image_src_video_render,
};

void register_my_plugin_image_source(void)
{
	obs_register_source(&image_src_info);
}
