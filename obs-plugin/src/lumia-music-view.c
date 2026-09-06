#include <obs-module.h>
#include <util/dstr.h>

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

OBS_DECLARE_MODULE()

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Lumia Music View";
}

#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 250
#define DEFAULT_PORT 8787
#define DEFAULT_BASE_URL "http://127.0.0.1:8787"
#define DEFAULT_CSS \
	"body { background-color: rgba(0, 0, 0, 0); margin: 0px; overflow: hidden; }"

struct lumia_source {
	obs_source_t *source;
	obs_source_t *browser;

	char *music_root;
	char *custom_css;
	char *base_url;
	uint32_t width;
	uint32_t height;
};

static const char *lumia_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "LumiaMusicView";
}

#ifdef _WIN32
static bool winhttp_post_json(INTERNET_PORT port, const wchar_t *path, const char *json_utf8)
{
	bool ok = false;
	HINTERNET session = WinHttpOpen(L"Lumia/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
					WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session)
		return false;

	HINTERNET conn = WinHttpConnect(session, L"127.0.0.1", port, 0);
	if (!conn) {
		WinHttpCloseHandle(session);
		return false;
	}

	HINTERNET req = WinHttpOpenRequest(conn, L"POST", path, NULL, WINHTTP_NO_REFERER,
					   WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
	if (!req) {
		WinHttpCloseHandle(conn);
		WinHttpCloseHandle(session);
		return false;
	}

	wchar_t headers[] = L"Content-Type: application/json\r\n";
	DWORD body_len = (DWORD)strlen(json_utf8);
	if (WinHttpSendRequest(req, headers, (DWORD)-1L, (LPVOID)json_utf8, body_len, body_len,
			       0) &&
	    WinHttpReceiveResponse(req, NULL)) {
		ok = true;
	}

	WinHttpCloseHandle(req);
	WinHttpCloseHandle(conn);
	WinHttpCloseHandle(session);
	return ok;
}
#endif

static int lumia_parse_port(const char *base_url)
{
	int port = DEFAULT_PORT;
	if (!base_url)
		return port;
	const char *p = strrchr(base_url, ':');
	if (p && p[1]) {
		int v = atoi(p + 1);
		if (v > 0 && v < 65536)
			port = v;
	}
	return port;
}

static void lumia_apply_library(struct lumia_source *ctx)
{
	if (!ctx->music_root || !*ctx->music_root)
		return;

#ifdef _WIN32
	struct dstr body = {0};
	dstr_copy(&body, "{\"root\":\"");
	for (const char *p = ctx->music_root; *p; ++p) {
		if (*p == '\\')
			dstr_cat(&body, "\\\\");
		else if (*p == '"')
			dstr_cat(&body, "\\\"");
		else
			dstr_ncat(&body, p, 1);
	}
	dstr_cat(&body, "\"}");

	if (!winhttp_post_json((INTERNET_PORT)lumia_parse_port(ctx->base_url), L"/api/library",
			       body.array)) {
		blog(LOG_WARNING,
		     "[Lumia] Could not POST library root - is `npm start` running?");
	} else {
		winhttp_post_json((INTERNET_PORT)lumia_parse_port(ctx->base_url), L"/api/control",
				  "{\"action\":\"play\"}");
	}
	dstr_free(&body);
#else
	UNUSED_PARAMETER(ctx);
#endif
}

static void lumia_update_browser(struct lumia_source *ctx)
{
	if (!ctx->browser)
		return;

	obs_data_t *settings = obs_source_get_settings(ctx->browser);
	struct dstr url = {0};
	dstr_printf(&url, "%s/overlay/",
		    (ctx->base_url && *ctx->base_url) ? ctx->base_url : DEFAULT_BASE_URL);

	obs_data_set_string(settings, "url", url.array);
	obs_data_set_int(settings, "width", ctx->width);
	obs_data_set_int(settings, "height", ctx->height);
	obs_data_set_bool(settings, "shutdown", true);
	obs_data_set_bool(settings, "restart_when_active", true);
	obs_data_set_string(settings, "css",
			    (ctx->custom_css && *ctx->custom_css) ? ctx->custom_css : DEFAULT_CSS);

	obs_source_update(ctx->browser, settings);
	obs_data_release(settings);
	dstr_free(&url);
}

static void lumia_ensure_browser(struct lumia_source *ctx)
{
	if (ctx->browser)
		return;

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "url", DEFAULT_BASE_URL "/overlay/");
	obs_data_set_int(settings, "width", ctx->width ? ctx->width : DEFAULT_WIDTH);
	obs_data_set_int(settings, "height", ctx->height ? ctx->height : DEFAULT_HEIGHT);
	obs_data_set_bool(settings, "shutdown", true);
	obs_data_set_string(settings, "css", DEFAULT_CSS);

	ctx->browser =
		obs_source_create_private("browser_source", "lumia_internal_browser", settings);
	obs_data_release(settings);

	if (ctx->browser) {
		obs_source_add_active_child(ctx->source, ctx->browser);
		lumia_update_browser(ctx);
	} else {
		blog(LOG_ERROR,
		     "[Lumia] Failed to create private browser_source (is obs-browser loaded?)");
	}
}

static void *lumia_create(obs_data_t *settings, obs_source_t *source)
{
	struct lumia_source *ctx = bzalloc(sizeof(struct lumia_source));
	ctx->source = source;
	ctx->width = DEFAULT_WIDTH;
	ctx->height = DEFAULT_HEIGHT;
	ctx->base_url = bstrdup(DEFAULT_BASE_URL);
	ctx->custom_css = bstrdup(DEFAULT_CSS);

	lumia_ensure_browser(ctx);
	obs_source_update(source, settings);
	return ctx;
}

static void lumia_destroy(void *data)
{
	struct lumia_source *ctx = data;

	if (ctx->browser) {
		obs_source_remove_active_child(ctx->source, ctx->browser);
		obs_source_release(ctx->browser);
		ctx->browser = NULL;
	}

	bfree(ctx->music_root);
	bfree(ctx->custom_css);
	bfree(ctx->base_url);
	bfree(ctx);
}

static void lumia_update(void *data, obs_data_t *settings)
{
	struct lumia_source *ctx = data;

	bfree(ctx->music_root);
	bfree(ctx->custom_css);
	bfree(ctx->base_url);

	ctx->music_root = bstrdup(obs_data_get_string(settings, "music_root"));
	ctx->custom_css = bstrdup(obs_data_get_string(settings, "css"));
	ctx->base_url = bstrdup(obs_data_get_string(settings, "base_url"));
	ctx->width = (uint32_t)obs_data_get_int(settings, "width");
	ctx->height = (uint32_t)obs_data_get_int(settings, "height");

	if (!ctx->width)
		ctx->width = DEFAULT_WIDTH;
	if (!ctx->height)
		ctx->height = DEFAULT_HEIGHT;
	if (!ctx->base_url || !*ctx->base_url) {
		bfree(ctx->base_url);
		ctx->base_url = bstrdup(DEFAULT_BASE_URL);
	}

	lumia_ensure_browser(ctx);
	lumia_update_browser(ctx);
	lumia_apply_library(ctx);
}

static uint32_t lumia_width(void *data)
{
	struct lumia_source *ctx = data;
	return ctx->width ? ctx->width : DEFAULT_WIDTH;
}

static uint32_t lumia_height(void *data)
{
	struct lumia_source *ctx = data;
	return ctx->height ? ctx->height : DEFAULT_HEIGHT;
}

static void lumia_video_render(void *data, gs_effect_t *effect)
{
	struct lumia_source *ctx = data;
	UNUSED_PARAMETER(effect);
	if (ctx->browser)
		obs_source_video_render(ctx->browser);
}

static void lumia_enum_active(void *data, obs_source_enum_proc_t cb, void *param)
{
	struct lumia_source *ctx = data;
	if (ctx->browser)
		cb(ctx->source, ctx->browser, param);
}

static void lumia_enum_all(void *data, obs_source_enum_proc_t cb, void *param)
{
	lumia_enum_active(data, cb, param);
}

static obs_properties_t *lumia_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_path(props, "music_root", "Music folder", OBS_PATH_DIRECTORY, NULL,
				NULL);
	obs_properties_add_int(props, "width", "Width", 100, 3840, 1);
	obs_properties_add_int(props, "height", "Height", 100, 2160, 1);
	obs_properties_add_text(props, "base_url", "Lumia server URL", OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "css", "Custom CSS", OBS_TEXT_MULTILINE);

	return props;
}

static void lumia_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", DEFAULT_WIDTH);
	obs_data_set_default_int(settings, "height", DEFAULT_HEIGHT);
	obs_data_set_default_string(settings, "base_url", DEFAULT_BASE_URL);
	obs_data_set_default_string(settings, "css", DEFAULT_CSS);
}

static struct obs_source_info lumia_source_info = {
	.id = "lumia_music_view",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_AUDIO |
			OBS_SOURCE_DO_NOT_DUPLICATE,
	.get_name = lumia_get_name,
	.create = lumia_create,
	.destroy = lumia_destroy,
	.update = lumia_update,
	.video_render = lumia_video_render,
	.get_width = lumia_width,
	.get_height = lumia_height,
	.get_properties = lumia_properties,
	.get_defaults = lumia_defaults,
	.enum_active_sources = lumia_enum_active,
	.enum_all_sources = lumia_enum_all,
	.icon_type = OBS_ICON_TYPE_MEDIA,
};

bool obs_module_load(void)
{
	obs_register_source(&lumia_source_info);
	blog(LOG_INFO, "[Lumia] LumiaMusicView registered - Live x Music x View");
	return true;
}

void obs_module_unload(void) {}
