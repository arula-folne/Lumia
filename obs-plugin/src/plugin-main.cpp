#include <obs-module.h>
#include <util/dstr.h>
#include <util/platform.h>

#include "lumia_http.hpp"

#include <cstring>
#include <string>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("lumia-music-view", "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Lumia Music View";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Lumia - Live x Music x View (local music overlay)";
}

#define DESIGN_WIDTH 1000
#define DESIGN_HEIGHT 250
#define DEFAULT_WIDTH DESIGN_WIDTH
#define DEFAULT_HEIGHT DESIGN_HEIGHT
#define DEFAULT_CSS \
	"body { background-color: rgba(0, 0, 0, 0); margin: 0px; overflow: hidden; }"
/* Browser overlay FPS — 30 is enough for music UI and lighter than 60 */
#define OVERLAY_FPS 30

#define S_BEHAVIOR "playback_behavior"
#define S_BEHAVIOR_STOP_RESTART "stop_restart"
#define S_BEHAVIOR_PAUSE_UNPAUSE "pause_unpause"
#define S_BEHAVIOR_ALWAYS_PLAY "always_play"

enum lumia_behavior {
	BEHAVIOR_STOP_RESTART = 0,
	BEHAVIOR_PAUSE_UNPAUSE,
	BEHAVIOR_ALWAYS_PLAY,
};

struct lumia_source {
	obs_source_t *source;
	obs_source_t *browser;
	obs_source_t *media; /* ffmpeg_source: actual audio/video decode like VLC/Media Source */
	LumiaHttpServer *server;

	char *custom_css;
	uint32_t width;
	uint32_t height;
	bool shuffle;
	bool loop;
	enum lumia_behavior behavior;

	uint64_t synced_generation;
	bool media_loaded;
};

static const char *lumia_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "LumiaMusicView";
}

static char *lumia_overlay_dir(void)
{
	return obs_module_file("overlay");
}

static enum lumia_behavior lumia_parse_behavior(const char *value)
{
	if (value && astrcmpi(value, S_BEHAVIOR_PAUSE_UNPAUSE) == 0)
		return BEHAVIOR_PAUSE_UNPAUSE;
	if (value && astrcmpi(value, S_BEHAVIOR_ALWAYS_PLAY) == 0)
		return BEHAVIOR_ALWAYS_PLAY;
	return BEHAVIOR_STOP_RESTART;
}

static std::vector<std::string> lumia_read_playlist(obs_data_t *settings)
{
	std::vector<std::string> paths;
	obs_data_array_t *arr = obs_data_get_array(settings, "playlist");
	if (!arr)
		return paths;

	const size_t count = obs_data_array_count(arr);
	paths.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		obs_data_t *item = obs_data_array_item(arr, i);
		const char *value = obs_data_get_string(item, "value");
		if (value && *value)
			paths.emplace_back(value);
		obs_data_release(item);
	}
	obs_data_array_release(arr);
	return paths;
}

static void lumia_load_media_file(struct lumia_source *ctx, const char *path, bool start_playback)
{
	if (!ctx->media || !path || !*path)
		return;

	obs_data_t *settings = obs_data_create();
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", path);
	obs_data_set_bool(settings, "looping", false);
	obs_data_set_bool(settings, "clear_on_media_end", true);
	obs_data_set_bool(settings, "restart_on_activate", false);
	obs_data_set_bool(settings, "close_when_inactive", false);
	obs_data_set_bool(settings, "hw_decode", false);
	obs_source_update(ctx->media, settings);
	obs_data_release(settings);

	ctx->media_loaded = true;
	ctx->synced_generation = ctx->server ? ctx->server->engine().mediaGeneration() : 0;

	if (start_playback) {
		obs_source_media_restart(ctx->media);
	} else {
		obs_source_media_stop(ctx->media);
	}
}

static void lumia_sync_media(struct lumia_source *ctx, bool force_reload)
{
	if (!ctx || !ctx->server || !ctx->media)
		return;

	auto &engine = ctx->server->engine();
	auto state = engine.snapshot();
	uint64_t gen = engine.mediaGeneration();

	std::string path;
	if (!engine.getCurrentFile(path)) {
		obs_source_media_stop(ctx->media);
		ctx->media_loaded = false;
		return;
	}

	const bool gen_changed = (gen != ctx->synced_generation);
	if (force_reload || gen_changed || !ctx->media_loaded) {
		lumia_load_media_file(ctx, path.c_str(), state.playing && !state.stopped);
		return;
	}

	enum obs_media_state ms = obs_source_media_get_state(ctx->media);
	if (state.stopped) {
		if (ms != OBS_MEDIA_STATE_STOPPED && ms != OBS_MEDIA_STATE_NONE)
			obs_source_media_stop(ctx->media);
		return;
	}

	if (state.playing) {
		if (ms == OBS_MEDIA_STATE_STOPPED || ms == OBS_MEDIA_STATE_ENDED ||
		    ms == OBS_MEDIA_STATE_NONE) {
			obs_source_media_restart(ctx->media);
		} else if (ms == OBS_MEDIA_STATE_PAUSED) {
			obs_source_media_play_pause(ctx->media, false);
		}
	} else {
		if (ms == OBS_MEDIA_STATE_PLAYING)
			obs_source_media_play_pause(ctx->media, true);
	}
}

static uint32_t lumia_pixel_width(const struct lumia_source *ctx)
{
	return ctx && ctx->width ? ctx->width : DEFAULT_WIDTH;
}

static uint32_t lumia_pixel_height(const struct lumia_source *ctx)
{
	return ctx && ctx->height ? ctx->height : DEFAULT_HEIGHT;
}

static void lumia_configure_browser_settings(obs_data_t *settings, struct lumia_source *ctx,
					     const char *url)
{
	obs_data_set_string(settings, "url", url);
	obs_data_set_int(settings, "width", (int)lumia_pixel_width(ctx));
	obs_data_set_int(settings, "height", (int)lumia_pixel_height(ctx));
	obs_data_set_int(settings, "fps", OVERLAY_FPS);
	obs_data_set_bool(settings, "shutdown", false);
	obs_data_set_bool(settings, "restart_when_active", false);
	/* Overlay is visual-only; audio comes from ffmpeg_source. */
	obs_data_set_bool(settings, "reroute_audio", false);
	obs_data_set_string(settings, "css",
			    (ctx->custom_css && *ctx->custom_css) ? ctx->custom_css : DEFAULT_CSS);
}

static void lumia_update_browser(struct lumia_source *ctx)
{
	if (!ctx->browser || !ctx->server || ctx->server->port() <= 0)
		return;

	obs_data_t *settings = obs_source_get_settings(ctx->browser);
	struct dstr url = {0};
	dstr_printf(&url, "%s/overlay/", ctx->server->baseUrl().c_str());
	lumia_configure_browser_settings(settings, ctx, url.array);
	obs_source_update(ctx->browser, settings);
	obs_data_release(settings);
	dstr_free(&url);
}

static bool lumia_ensure_server(struct lumia_source *ctx)
{
	if (ctx->server && ctx->server->port() > 0)
		return true;

	if (!ctx->server)
		ctx->server = new LumiaHttpServer();

	char *dir = lumia_overlay_dir();
	if (!dir) {
		blog(LOG_ERROR,
		     "[Lumia] overlay data missing (data/obs-plugins/lumia-music-view/overlay)");
		return false;
	}
	std::string root(dir);
	bfree(dir);

	if (!ctx->server->start(root, 18787)) {
		blog(LOG_ERROR, "[Lumia] failed to start embedded server");
		return false;
	}
	blog(LOG_INFO, "[Lumia] embedded server at %s", ctx->server->baseUrl().c_str());
	return true;
}

static void lumia_capture_audio(void *param, obs_source_t *src, const struct audio_data *audio_data, bool muted)
{
	UNUSED_PARAMETER(src);
	auto *ctx = (lumia_source *)param;
	if (!ctx || !ctx->source || !audio_data || muted)
		return;
	if (!audio_data->frames || !audio_data->data[0])
		return;

	/* Don't feed silence into parent when transport is stopped. */
	if (ctx->server) {
		auto st = ctx->server->engine().snapshot();
		if (st.stopped)
			return;
	}

	audio_t *audio = obs_get_audio();
	if (!audio)
		return;

	struct obs_source_audio out = {};
	out.frames = audio_data->frames;
	out.timestamp = audio_data->timestamp;
	out.samples_per_sec = audio_output_get_sample_rate(audio);
	out.format = AUDIO_FORMAT_FLOAT_PLANAR;
	out.speakers = (enum speaker_layout)audio_output_get_channels(audio);

	for (size_t i = 0; i < MAX_AV_PLANES; i++)
		out.data[i] = audio_data->data[i];

	obs_source_output_audio(ctx->source, &out);
}

static void lumia_ensure_media(struct lumia_source *ctx)
{
	if (ctx->media)
		return;

	obs_data_t *settings = obs_data_create();
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", "");
	obs_data_set_bool(settings, "looping", false);
	obs_data_set_bool(settings, "clear_on_media_end", true);
	obs_data_set_bool(settings, "restart_on_activate", false);
	obs_data_set_bool(settings, "close_when_inactive", false);

	ctx->media = obs_source_create_private("ffmpeg_source", "lumia_media", settings);
	obs_data_release(settings);

	if (ctx->media) {
		obs_source_add_active_child(ctx->source, ctx->media);
		obs_source_set_volume(ctx->media, 1.0f);
		obs_source_set_muted(ctx->media, false);
		obs_source_set_audio_mixers(ctx->media, 0xFF);
		/* Private ffmpeg audio is not mixed to output by itself; capture and
		 * re-output on the parent so the mixer fader works. */
		obs_source_add_audio_capture_callback(ctx->media, lumia_capture_audio, ctx);
		blog(LOG_INFO, "[Lumia] ffmpeg media source ready");
	} else {
		blog(LOG_ERROR, "[Lumia] failed to create ffmpeg_source");
	}
}

static void lumia_ensure_browser(struct lumia_source *ctx)
{
	if (ctx->browser)
		return;
	if (!lumia_ensure_server(ctx))
		return;

	obs_data_t *settings = obs_data_create();
	struct dstr url = {0};
	dstr_printf(&url, "%s/overlay/", ctx->server->baseUrl().c_str());
	lumia_configure_browser_settings(settings, ctx, url.array);

	ctx->browser =
		obs_source_create_private("browser_source", "lumia_internal_browser", settings);
	obs_data_release(settings);
	dstr_free(&url);

	if (ctx->browser) {
		obs_source_add_active_child(ctx->source, ctx->browser);
		lumia_update_browser(ctx);
	} else {
		blog(LOG_ERROR, "[Lumia] browser_source unavailable");
	}
}

static void lumia_apply_playlist(struct lumia_source *ctx, obs_data_t *settings)
{
	if (!ctx->server)
		return;

	ctx->server->engine().setShuffle(ctx->shuffle);
	ctx->server->engine().setLoop(ctx->loop);

	auto paths = lumia_read_playlist(settings);
	std::string err;
	if (!ctx->server->engine().setPlaylist(paths, err)) {
		blog(LOG_WARNING, "[Lumia] playlist: %s", err.c_str());
		obs_source_media_ended(ctx->source);
		lumia_sync_media(ctx, true);
		return;
	}

	/* Load first track but stay stopped (VLC does not autoplay on list edit by default for us). */
	ctx->server->engine().stop();
	lumia_sync_media(ctx, true);
	obs_source_media_ended(ctx->source);
}

static void lumia_activate(void *data)
{
	auto *ctx = (lumia_source *)data;
	if (!ctx || !ctx->server)
		return;

	if (ctx->behavior == BEHAVIOR_STOP_RESTART) {
		ctx->server->engine().restart();
		lumia_sync_media(ctx, true);
		obs_source_media_started(ctx->source);
	} else if (ctx->behavior == BEHAVIOR_PAUSE_UNPAUSE) {
		ctx->server->engine().play();
		lumia_sync_media(ctx, false);
		obs_source_media_started(ctx->source);
	}
}

static void lumia_deactivate(void *data)
{
	auto *ctx = (lumia_source *)data;
	if (!ctx || !ctx->server)
		return;

	if (ctx->behavior == BEHAVIOR_STOP_RESTART) {
		ctx->server->engine().stop();
		lumia_sync_media(ctx, false);
		obs_source_media_ended(ctx->source);
	} else if (ctx->behavior == BEHAVIOR_PAUSE_UNPAUSE) {
		ctx->server->engine().pause();
		lumia_sync_media(ctx, false);
	}
}

static void *lumia_create(obs_data_t *settings, obs_source_t *source)
{
	auto *ctx = (lumia_source *)bzalloc(sizeof(lumia_source));
	ctx->source = source;
	ctx->width = DEFAULT_WIDTH;
	ctx->height = DEFAULT_HEIGHT;
	ctx->custom_css = bstrdup(DEFAULT_CSS);
	ctx->shuffle = true;
	ctx->loop = true;
	ctx->behavior = BEHAVIOR_STOP_RESTART;
	ctx->server = nullptr;
	ctx->synced_generation = 0;
	ctx->media_loaded = false;

	lumia_ensure_server(ctx);
	lumia_ensure_media(ctx);
	lumia_ensure_browser(ctx);
	/* Hot masters/WAV often clip at 100%; start quieter. Adjust via mixer. */
	obs_source_set_volume(source, 0.4f);
	obs_source_update(source, settings);
	return ctx;
}

static void lumia_destroy(void *data)
{
	auto *ctx = (lumia_source *)data;
	if (ctx->browser) {
		obs_source_remove_active_child(ctx->source, ctx->browser);
		obs_source_release(ctx->browser);
		ctx->browser = nullptr;
	}
	if (ctx->media) {
		obs_source_remove_audio_capture_callback(ctx->media, lumia_capture_audio, ctx);
		obs_source_remove_active_child(ctx->source, ctx->media);
		obs_source_release(ctx->media);
		ctx->media = nullptr;
	}
	if (ctx->server) {
		delete ctx->server;
		ctx->server = nullptr;
	}
	bfree(ctx->custom_css);
	bfree(ctx);
}

static void lumia_update(void *data, obs_data_t *settings)
{
	auto *ctx = (lumia_source *)data;

	bfree(ctx->custom_css);
	ctx->custom_css = bstrdup(obs_data_get_string(settings, "css"));
	ctx->width = (uint32_t)obs_data_get_int(settings, "width");
	ctx->height = (uint32_t)obs_data_get_int(settings, "height");
	ctx->shuffle = obs_data_get_bool(settings, "shuffle");
	ctx->loop = obs_data_get_bool(settings, "loop");
	ctx->behavior = lumia_parse_behavior(obs_data_get_string(settings, S_BEHAVIOR));
	if (!ctx->width)
		ctx->width = DEFAULT_WIDTH;
	if (!ctx->height)
		ctx->height = DEFAULT_HEIGHT;

	lumia_ensure_server(ctx);
	lumia_ensure_media(ctx);
	lumia_ensure_browser(ctx);
	lumia_update_browser(ctx);
	lumia_apply_playlist(ctx, settings);
}

static void lumia_video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	auto *ctx = (lumia_source *)data;
	if (!ctx || !ctx->server || !ctx->media)
		return;

	auto &engine = ctx->server->engine();
	auto state = engine.snapshot();

	if (ctx->media_loaded) {
		int64_t t = obs_source_media_get_time(ctx->media);
		int64_t d = obs_source_media_get_duration(ctx->media);
		if (t >= 0 && d >= 0)
			engine.setTransportTimes((double)t / 1000.0, (double)d / 1000.0);

		enum obs_media_state ms = obs_source_media_get_state(ctx->media);
		if (ms == OBS_MEDIA_STATE_ENDED && state.playing && !state.stopped) {
			engine.next();
			lumia_sync_media(ctx, true);
			if (engine.snapshot().playing)
				obs_source_media_started(ctx->source);
			else
				obs_source_media_ended(ctx->source);
			return;
		}
	}

	if (engine.mediaGeneration() != ctx->synced_generation)
		lumia_sync_media(ctx, true);
}

/* ---- Media controls (same roles as VLC) ---- */

static void lumia_media_play_pause(void *data, bool pause)
{
	auto *ctx = (lumia_source *)data;
	if (!ctx || !ctx->server)
		return;

	if (pause) {
		ctx->server->engine().pause();
		lumia_sync_media(ctx, false);
	} else {
		auto st = ctx->server->engine().snapshot();
		if (st.stopped || !st.hasTrack) {
			ctx->server->engine().restart();
			lumia_sync_media(ctx, true);
		} else {
			ctx->server->engine().play();
			lumia_sync_media(ctx, false);
		}
		obs_source_media_started(ctx->source);
	}
}

static void lumia_media_restart(void *data)
{
	auto *ctx = (lumia_source *)data;
	if (!ctx || !ctx->server)
		return;
	ctx->server->engine().restart();
	lumia_sync_media(ctx, true);
	obs_source_media_started(ctx->source);
}

static void lumia_media_stop(void *data)
{
	auto *ctx = (lumia_source *)data;
	if (!ctx || !ctx->server)
		return;
	ctx->server->engine().stop();
	lumia_sync_media(ctx, false);
	obs_source_media_ended(ctx->source);
}

static void lumia_media_next(void *data)
{
	auto *ctx = (lumia_source *)data;
	if (!ctx || !ctx->server)
		return;
	ctx->server->engine().next();
	lumia_sync_media(ctx, true);
	if (ctx->server->engine().snapshot().playing)
		obs_source_media_started(ctx->source);
}

static void lumia_media_previous(void *data)
{
	auto *ctx = (lumia_source *)data;
	if (!ctx || !ctx->server)
		return;
	ctx->server->engine().prev();
	lumia_sync_media(ctx, true);
	obs_source_media_started(ctx->source);
}

static int64_t lumia_media_get_duration(void *data)
{
	auto *ctx = (lumia_source *)data;
	if (!ctx || !ctx->media)
		return 0;
	return obs_source_media_get_duration(ctx->media);
}

static int64_t lumia_media_get_time(void *data)
{
	auto *ctx = (lumia_source *)data;
	if (!ctx || !ctx->media)
		return 0;
	return obs_source_media_get_time(ctx->media);
}

static void lumia_media_set_time(void *data, int64_t milliseconds)
{
	auto *ctx = (lumia_source *)data;
	if (!ctx || !ctx->media)
		return;
	obs_source_media_set_time(ctx->media, milliseconds);
	if (ctx->server)
		ctx->server->engine().setTransportTimes((double)milliseconds / 1000.0, -1);
}

static enum obs_media_state lumia_media_get_state(void *data)
{
	auto *ctx = (lumia_source *)data;
	if (!ctx || !ctx->server)
		return OBS_MEDIA_STATE_NONE;

	auto s = ctx->server->engine().snapshot();
	if (!s.hasTrack)
		return OBS_MEDIA_STATE_NONE;
	if (s.stopped)
		return OBS_MEDIA_STATE_STOPPED;
	if (s.playing)
		return OBS_MEDIA_STATE_PLAYING;
	return OBS_MEDIA_STATE_PAUSED;
}

static uint32_t lumia_width(void *data)
{
	return lumia_pixel_width((lumia_source *)data);
}

static uint32_t lumia_height(void *data)
{
	return lumia_pixel_height((lumia_source *)data);
}

static void lumia_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	auto *ctx = (lumia_source *)data;
	if (ctx->browser)
		obs_source_video_render(ctx->browser);
}

static void lumia_enum_active(void *data, obs_source_enum_proc_t cb, void *param)
{
	auto *ctx = (lumia_source *)data;
	if (ctx->media)
		cb(ctx->source, ctx->media, param);
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

	obs_properties_add_bool(props, "loop", obs_module_text("LoopPlaylist"));
	obs_properties_add_bool(props, "shuffle", obs_module_text("ShufflePlaylist"));

	obs_property_t *p = obs_properties_add_list(props, S_BEHAVIOR, obs_module_text("PlaybackBehavior"),
						    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("PlaybackBehavior.StopRestart"),
				     S_BEHAVIOR_STOP_RESTART);
	obs_property_list_add_string(p, obs_module_text("PlaybackBehavior.PauseUnpause"),
				     S_BEHAVIOR_PAUSE_UNPAUSE);
	obs_property_list_add_string(p, obs_module_text("PlaybackBehavior.AlwaysPlay"),
				     S_BEHAVIOR_ALWAYS_PLAY);

	/* Files and folders (album dirs) via the same playlist list */
	obs_properties_add_editable_list(props, "playlist", obs_module_text("Playlist"),
					 OBS_EDITABLE_LIST_TYPE_FILES_AND_URLS, NULL, NULL);
	obs_properties_add_int(props, "width", obs_module_text("Width"), 100, 3840, 1);
	obs_properties_add_int(props, "height", obs_module_text("Height"), 100, 2160, 1);
	obs_properties_add_text(props, "css", obs_module_text("CustomCSS"), OBS_TEXT_MULTILINE);

	return props;
}

static void lumia_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "loop", true);
	obs_data_set_default_bool(settings, "shuffle", true);
	obs_data_set_default_string(settings, S_BEHAVIOR, S_BEHAVIOR_STOP_RESTART);
	obs_data_set_default_int(settings, "width", DEFAULT_WIDTH);
	obs_data_set_default_int(settings, "height", DEFAULT_HEIGHT);
	obs_data_set_default_string(settings, "css", DEFAULT_CSS);
}

static struct obs_source_info lumia_source_info = {};

static void lumia_init_source_info(void)
{
	lumia_source_info.id = "lumia_music_view";
	lumia_source_info.type = OBS_SOURCE_TYPE_INPUT;
	lumia_source_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW |
					 OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE |
					 OBS_SOURCE_CONTROLLABLE_MEDIA | OBS_SOURCE_MONITOR_BY_DEFAULT;
	lumia_source_info.get_name = lumia_get_name;
	lumia_source_info.create = lumia_create;
	lumia_source_info.destroy = lumia_destroy;
	lumia_source_info.update = lumia_update;
	lumia_source_info.activate = lumia_activate;
	lumia_source_info.deactivate = lumia_deactivate;
	lumia_source_info.video_tick = lumia_video_tick;
	lumia_source_info.video_render = lumia_video_render;
	lumia_source_info.get_width = lumia_width;
	lumia_source_info.get_height = lumia_height;
	lumia_source_info.get_properties = lumia_properties;
	lumia_source_info.get_defaults = lumia_defaults;
	lumia_source_info.enum_active_sources = lumia_enum_active;
	lumia_source_info.enum_all_sources = lumia_enum_all;
	lumia_source_info.icon_type = OBS_ICON_TYPE_MEDIA;
	lumia_source_info.media_play_pause = lumia_media_play_pause;
	lumia_source_info.media_restart = lumia_media_restart;
	lumia_source_info.media_stop = lumia_media_stop;
	lumia_source_info.media_next = lumia_media_next;
	lumia_source_info.media_previous = lumia_media_previous;
	lumia_source_info.media_get_duration = lumia_media_get_duration;
	lumia_source_info.media_get_time = lumia_media_get_time;
	lumia_source_info.media_set_time = lumia_media_set_time;
	lumia_source_info.media_get_state = lumia_media_get_state;
}

bool obs_module_load(void)
{
	lumia_init_source_info();
	obs_register_source(&lumia_source_info);
	blog(LOG_INFO, "[Lumia] LumiaMusicView ready (ffmpeg playback like VLC)");
	return true;
}

void obs_module_unload(void) {}
