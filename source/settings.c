/* settings.c -- settings and play/backup statistics
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <string.h>
#include <jansson.h>
#include "settings.h"
#include "jsonfile.h"
#include "audio.h"

Settings settings = {
	.resolution = RES_AUTO, .widescreen = false, .full_width = true, .overscan = 0, .intro = true,
	.music_volume = VOLUME_MAX, .menu_volume = 6,
	.loop_count = 2, .fade_seconds = 10, .loop_forever = false, .track_end = END_NEXT,
	.launcher = "",
};
Stats stats_all, stats_session;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int get_int(json_t *o, const char *key, int def, int lo, int hi) {
	json_t *v = json_object_get(o, key);
	return json_is_integer(v) ? clampi((int)json_integer_value(v), lo, hi) : def;
}

static bool get_bool(json_t *o, const char *key, bool def) {
	json_t *v = json_object_get(o, key);
	return json_is_boolean(v) ? json_is_true(v) : def;
}

void settings_load(void) {
	json_t *root = json_load(SETTINGS_FILE);
	if (root) {
		Settings *s = &settings;
		json_t *d = json_object_get(root, "display");
		s->resolution = get_int(d, "resolution", s->resolution, 0, RES_COUNT - 1);
		s->widescreen = get_bool(d, "widescreen", s->widescreen);
		s->full_width = get_bool(d, "full_width", s->full_width);
		s->overscan = get_int(d, "overscan", s->overscan, 0, OVERSCAN_MAX);
		s->intro = get_bool(d, "intro", s->intro);
		json_t *snd = json_object_get(root, "sound");
		s->music_volume = get_int(snd, "music_volume", s->music_volume, 0, VOLUME_MAX);
		s->menu_volume = get_int(snd, "menu_volume", s->menu_volume, 0, VOLUME_MAX);
		json_t *p = json_object_get(root, "playback");
		s->loop_count = get_int(p, "loop_count", s->loop_count, 1, 9);
		s->fade_seconds = get_int(p, "fade_seconds", s->fade_seconds, 0, 20);
		s->loop_forever = get_bool(p, "loop_forever", s->loop_forever);
		s->track_end = get_int(p, "track_end", s->track_end, 0, END_COUNT - 1);
		s->saved_sort = get_int(root, "saved_sort", s->saved_sort, 0, SORT_COUNT - 1);
		const char *l = json_string_value(json_object_get(root, "launcher"));
		snprintf(s->launcher, sizeof(s->launcher), "%s", l ? l : "");
		json_t *st = json_object_get(root, "stats");
		json_t *v = json_object_get(st, "seconds_played");
		if (json_is_number(v))
			stats_all.seconds_played = json_number_value(v);
		v = json_object_get(st, "bytes_backed_up");
		if (json_is_integer(v))
			stats_all.bytes_backed_up = (uint64_t)json_integer_value(v);
		json_decref(root);
	}
	settings_apply_audio();
}

bool settings_save(void) {
	const Settings *s = &settings;
	json_t *root = json_pack("{s:{s:i,s:b,s:b,s:i,s:b},s:{s:i,s:i},s:{s:i,s:i,s:b,s:i},s:s,s:{s:I,s:I}}",
		"display", "resolution", s->resolution, "widescreen", s->widescreen, "full_width", s->full_width,
		           "overscan", s->overscan, "intro", s->intro,
		"sound", "music_volume", s->music_volume, "menu_volume", s->menu_volume,
		"playback", "loop_count", s->loop_count, "fade_seconds", s->fade_seconds,
		            "loop_forever", s->loop_forever, "track_end", s->track_end,
		"launcher", s->launcher,
		"stats", "seconds_played", (json_int_t)stats_all.seconds_played,
		         "bytes_backed_up", (json_int_t)stats_all.bytes_backed_up);
	if (!root)
		return false;
	json_object_set_new(root, "saved_sort", json_integer(s->saved_sort));
	bool ok = json_save(SETTINGS_FILE, root);
	json_decref(root);
	return ok;
}

void settings_apply_audio(void) {
	audio_settings.loop_count = settings.loop_count;
	audio_settings.fade_seconds = settings.fade_seconds;
	audio_settings.loop_forever = settings.loop_forever;
	audio_set_volume(settings.music_volume / (float)VOLUME_MAX);
}

void stats_add_played(double seconds) {
	stats_session.seconds_played += seconds;
	stats_all.seconds_played += seconds;
}

void stats_add_backed_up(uint64_t bytes) {
	stats_session.bytes_backed_up += bytes;
	stats_all.bytes_backed_up += bytes;
}
