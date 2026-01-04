#include <obs-module.h>
#include <graphics/graphics.h>
#include <plugin-support.h>

#include "plugin-text-source.h"
#include "plugin-properties.h"
#include "plugin-browser.h"
#include "plugin-xbox-auth.h"

struct text_src {
	obs_source_t *source;
	char *text;

	obs_source_t *child_text; /* built-in text source */

	uint32_t width;
	uint32_t height;
};

/* Gets the name of the source */
const char *text_src_get_name(
    void *unused
)
{
	UNUSED_PARAMETER(unused);
	return "Achievements Tracker";
}

static bool on_sign_in_xbox_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	UNUSED_PARAMETER(data);

	/* TODO: move to OBS settings UI */
	const char *client_id = "YOUR_CLIENT_ID_HERE";
	const char *scope = "XboxLive.signin offline_access";

	char *uhs = NULL;
	char *xsts_token = NULL;
	if (!xbox_auth_interactive_get_xsts(client_id, scope, &uhs, &xsts_token)) {
		obs_log(LOG_WARNING, "Xbox sign-in failed");
		return true;
	}

	obs_log(LOG_INFO, "Use for Xbox APIs: Authorization: XBL3.0 x=%s;%s", uhs, xsts_token);
	bfree(uhs);
	bfree(xsts_token);
	return true;
}

/* Gets the properties of the source */
obs_properties_t *get_properties(
    void *data
)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_text(p, "text", "Text", OBS_TEXT_DEFAULT);
	obs_properties_add_button(p, "sign-in-xbox", "Sign in with Xbox", &on_sign_in_xbox_clicked);
	return p;
}

static void text_src_update_child(
    struct text_src *s
)
{
	if (!s || !s->child_text)
		return;

	obs_data_t *st = obs_data_create();
	obs_data_set_string(st, "text", s->text ? s->text : "");
	obs_data_set_int(st, "color1", 0xFFFFFFFF); /* white */
	obs_data_set_bool(st, "outline", false);
	obs_data_set_int(st, "align", 0);  /* left */
	obs_data_set_int(st, "valign", 0); /* top */
	obs_source_update(s->child_text, st);
	obs_data_release(st);
}

void *text_src_create(
    obs_data_t *settings,
    obs_source_t *source
)
{
	struct text_src *s = bzalloc(sizeof(*s));
	s->source = source;

	const char *t = obs_data_get_string(settings, "text");
	s->text = bstrdup(t ? t : "Hello from plugin");

	s->width = 800;
	s->height = 200;

	/* Create built-in text source as a child */
	obs_data_t *st = obs_data_create();
	obs_data_set_string(st, "text", s->text);
	s->child_text =
		obs_source_create_private("text_ft2_source", "child_text", st);
	obs_data_release(st);

	text_src_update_child(s);
	return s;
}

void text_src_destroy(
    void *data
)
{
	struct text_src *s = data;

	if (!s)
		return;

	if (s->child_text)
		obs_source_release(s->child_text);

	bfree(s->text);
	bfree(s);
}

void text_src_update(
    void *data,
    obs_data_t *settings
)
{
	struct text_src *s = data;
	const char *t = obs_data_get_string(settings, "text");

	bfree(s->text);
	s->text = bstrdup(t ? t : "");

	text_src_update_child(s);
}

uint32_t text_src_get_width(
    void *data
)
{
	struct text_src *s = data;
	return s->width;
}

uint32_t text_src_get_height(
    void *data
)
{
	struct text_src *s = data;
	return s->height;
}

void text_src_video_render(
    void *data,
    gs_effect_t *effect
)
{
	UNUSED_PARAMETER(effect);
	struct text_src *s = data;
	if (!s || !s->child_text)
		return;

	/* Let the child (Text FT2) render into our source */
	obs_source_video_render(s->child_text);
}

void register_my_plugin_text_source(void)
{
	obs_register_source(get_plugin_properties());
}
