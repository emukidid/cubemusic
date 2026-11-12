/* trackinfo.c -- what vgmstream reported about each track, cached on SD
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "trackinfo.h"
#include "jsonfile.h"

static TrackInfo *find(InfoCache *c, const char *path) {
	for (int i = 0; i < c->count; i++)
		if (!strcasecmp(c->items[i].path, path))
			return &c->items[i];
	return NULL;
}

static TrackInfo *add(InfoCache *c, const char *path) {
	TrackInfo *t = find(c, path);
	if (t)
		return t;
	if (c->count == c->cap) {
		int cap = c->cap ? c->cap * 2 : 64;
		TrackInfo *n = realloc(c->items, cap * sizeof(TrackInfo));
		if (!n)
			return NULL;
		c->items = n;
		c->cap = cap;
	}
	t = &c->items[c->count++];
	memset(t, 0, sizeof(*t));
	t->path = strdup(path);
	return t;
}

static int64_t num(const json_t *o, const char *key) {
	const json_t *v = json_object_get(o, key);
	return json_is_integer(v) ? (int64_t)json_integer_value(v) : (int64_t)json_number_value(v);
}

void info_load(InfoCache *c, const char *game_id) {
	info_free(c);
	LWP_MutexInit(&c->lock, false);
	memcpy(c->game_id, game_id, 6);
	c->game_id[6] = 0;

	char file[64];
	snprintf(file, sizeof(file), CACHE_DIR "/%s.json", c->game_id);
	json_t *root = json_load(file);
	if (!root)
		return;
	const char *path;
	json_t *item;
	json_object_foreach(json_object_get(root, "tracks"), path, item) {
		TrackInfo *t = add(c, path);
		if (!t)
			break;
		t->sample_rate = num(item, "rate");
		t->channels = num(item, "channels");
		t->samples = num(item, "samples");
		t->loop = json_is_true(json_object_get(item, "loop"));
		t->loop_start = num(item, "loop_start");
		t->loop_end = num(item, "loop_end");
		t->playable = !json_is_false(json_object_get(item, "playable"));
		const char *codec = json_string_value(json_object_get(item, "codec"));
		if (codec)
			snprintf(t->codec, sizeof(t->codec), "%s", codec);
	}
	json_decref(root);
}

void info_free(InfoCache *c) {
	if (c->lock != LWP_MUTEX_NULL && c->game_id[0])
		LWP_MutexDestroy(c->lock);
	for (int i = 0; i < c->count; i++)
		free(c->items[i].path);
	free(c->items);
	memset(c, 0, sizeof(*c));
	c->lock = LWP_MUTEX_NULL;
}

bool info_save(InfoCache *c) {
	if (!c->game_id[0])
		return false;
	LWP_MutexLock(c->lock);
	if (!c->dirty) {
		LWP_MutexUnlock(c->lock);
		return true;
	}
	json_t *root = json_object();
	json_t *tracks = json_object();
	for (int i = 0; i < c->count; i++) {
		const TrackInfo *t = &c->items[i];
		json_t *o = json_object();
		if (!t->playable) {
			json_object_set_new(o, "playable", json_false());
		} else {
			json_object_set_new(o, "rate", json_integer(t->sample_rate));
			json_object_set_new(o, "channels", json_integer(t->channels));
			json_object_set_new(o, "samples", json_integer(t->samples));
			if (t->loop) {
				json_object_set_new(o, "loop", json_true());
				json_object_set_new(o, "loop_start", json_integer(t->loop_start));
				json_object_set_new(o, "loop_end", json_integer(t->loop_end));
			}
			json_object_set_new(o, "codec", json_string(t->codec));
		}
		json_object_set_new(tracks, t->path, o);
	}
	json_object_set_new(root, "tracks", tracks);
	c->dirty = false;
	LWP_MutexUnlock(c->lock);

	char file[64];
	snprintf(file, sizeof(file), CACHE_DIR "/%s.json", c->game_id);
	bool ok = json_save(file, root);
	json_decref(root);
	if (!ok)
		c->dirty = true;   /* try again next time (e.g. no SD card yet) */
	return ok;
}

bool info_get(InfoCache *c, const char *path, TrackInfo *out) {
	if (!c->game_id[0])
		return false;
	LWP_MutexLock(c->lock);
	TrackInfo *t = find(c, path);
	if (t)
		*out = *t;
	LWP_MutexUnlock(c->lock);
	return t != NULL;
}

void info_record(InfoCache *c, const char *path, const libvgmstream_format_t *fmt) {
	if (!c->game_id[0])
		return;
	LWP_MutexLock(c->lock);
	bool is_new = find(c, path) == NULL;
	TrackInfo *t = add(c, path);
	if (t) {
		TrackInfo n = { .path = t->path };
		if (fmt) {
			n.playable = true;
			n.sample_rate = fmt->sample_rate;
			n.channels = fmt->input_channels ? fmt->input_channels : fmt->channels;
			n.samples = fmt->stream_samples;
			n.loop = fmt->loop_flag;
			n.loop_start = fmt->loop_start;
			n.loop_end = fmt->loop_end;
			snprintf(n.codec, sizeof(n.codec), "%s", fmt->codec_name);
		}
		if (is_new || n.playable != t->playable || n.sample_rate != t->sample_rate ||
		    n.channels != t->channels || n.samples != t->samples || n.loop != t->loop ||
		    n.loop_start != t->loop_start || n.loop_end != t->loop_end || strcmp(n.codec, t->codec))
			c->dirty = true;
		*t = n;
	}
	LWP_MutexUnlock(c->lock);
}

void info_probe(InfoCache *c, const char *key, libstreamfile_t *sf, int subsong) {
	libvgmstream_config_t cfg = {0};
	libvgmstream_t *lib = sf ? libvgmstream_create(sf, subsong, &cfg) : NULL;
	info_record(c, key, lib ? lib->format : NULL);
	if (lib)
		libvgmstream_free(lib);
}

float info_duration(const TrackInfo *t, const AudioSettings *s) {
	if (!t->playable || t->sample_rate <= 0)
		return 0;
	if (!t->loop || t->loop_end <= t->loop_start)
		return (float)t->samples / t->sample_rate;
	if (s->loop_forever)
		return 0;
	double samples = t->loop_start + (double)(t->loop_end - t->loop_start) * s->loop_count
	                 + (double)s->fade_seconds * t->sample_rate;
	return (float)(samples / t->sample_rate);
}
