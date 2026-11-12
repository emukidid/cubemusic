/* names.c -- per-game track names and corrections, kept on SD
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "names.h"
#include "jsonfile.h"

static void names_path(char *out, size_t size, const char *game_id, bool regionless) {
	char id[7];
	memcpy(id, game_id, 6);
	id[6] = 0;
	if (regionless)
		id[3] = '_';
	snprintf(out, size, NAMES_DIR "/%s.json", id);
}

static NameEntry *find_mut(NameTable *t, const char *path) {
	for (int i = 0; i < t->count; i++)
		if (!strcasecmp(t->entries[i].path, path))
			return &t->entries[i];
	return NULL;
}

/* entry path is the end of path, on a folder boundary ("x.dsp" vs "Audio/x.dsp") */
static bool is_tail(const char *path, const char *tail) {
	size_t pl = strlen(path), tl = strlen(tail);
	return tl < pl && path[pl - tl - 1] == '/' && !strcasecmp(path + pl - tl, tail);
}

const NameEntry *names_find(const NameTable *t, const char *path) {
	const NameEntry *e = find_mut((NameTable *)t, path);
	for (int i = 0; !e && i < t->count; i++)
		if (is_tail(path, t->entries[i].path))
			e = &t->entries[i];
	return e;
}

static NameEntry *add_entry(NameTable *t, const char *path) {
	NameEntry *e = find_mut(t, path);
	if (e)
		return e;
	if (t->count == t->cap) {
		int cap = t->cap ? t->cap * 2 : 32;
		NameEntry *n = realloc(t->entries, cap * sizeof(NameEntry));
		if (!n)
			return NULL;
		t->entries = n;
		t->cap = cap;
	}
	e = &t->entries[t->count++];
	memset(e, 0, sizeof(*e));
	e->path = strdup(path);
	return e;
}

void names_set(NameTable *t, const char *path, const char *name, bool hidden) {
	NameEntry *e = add_entry(t, path);
	if (!e)
		return;
	free(e->name);
	e->name = (name && *name) ? strdup(name) : NULL;
	e->hidden = hidden;
}

void names_remove(NameTable *t, const char *path) {
	NameEntry *e = find_mut(t, path);
	if (!e)
		return;
	free(e->path);
	free(e->name);
	int i = e - t->entries;
	memmove(e, e + 1, (t->count - i - 1) * sizeof(NameEntry));
	t->count--;
}

void names_add(NameTable *t, const char *path) {
	NameEntry *e = add_entry(t, path);
	if (e)
		e->keep = true;
}

static bool load_file(NameTable *t, const char *file) {
	json_t *root = json_load(file);
	if (!root)
		return false;
	const char *title = json_string_value(json_object_get(root, "title"));
	if (title)
		snprintf(t->title, sizeof(t->title), "%s", title);
	size_t i;
	json_t *item;
	json_array_foreach(json_object_get(root, "tracks"), i, item) {
		const char *path = json_string_value(json_object_get(item, "path"));
		if (!path || !path[0])
			continue;
		names_set(t, path, json_string_value(json_object_get(item, "name")),
		          json_is_true(json_object_get(item, "hidden")));
		NameEntry *e = find_mut(t, path);
		if (e)
			e->keep = true;   /* (a path on its own adds a track: keep it when saving) */
	}
	json_decref(root);
	return true;
}

bool names_load(NameTable *t, const char *game_id) {
	names_free(t);
	memcpy(t->game_id, game_id, 6);
	t->game_id[6] = 0;
	char file[64];
	names_path(file, sizeof(file), game_id, false);
	if (load_file(t, file))
		return true;
	names_path(file, sizeof(file), game_id, true);
	if (load_file(t, file)) {
		t->from_regionless = true;
		return true;
	}
	return false;
}

void names_free(NameTable *t) {
	for (int i = 0; i < t->count; i++) {
		free(t->entries[i].path);
		free(t->entries[i].name);
	}
	free(t->entries);
	memset(t, 0, sizeof(*t));
}

bool names_save(const NameTable *t) {
	json_t *root = json_object();
	if (t->title[0])
		json_object_set_new(root, "title", json_string(t->title));
	json_t *tracks = json_array();
	for (int i = 0; i < t->count; i++) {
		const NameEntry *e = &t->entries[i];
		if (!e->name && !e->hidden && !e->keep)
			continue;
		json_t *o = json_object();
		json_object_set_new(o, "path", json_string(e->path));
		if (e->name)
			json_object_set_new(o, "name", json_string(e->name));
		if (e->hidden)
			json_object_set_new(o, "hidden", json_true());
		json_array_append_new(tracks, o);
	}
	json_object_set_new(root, "tracks", tracks);
	char file[64];
	names_path(file, sizeof(file), t->game_id, false);
	bool ok = json_save(file, root);
	json_decref(root);
	return ok;
}

bool names_export(const char *game_id, const char *title, const ExportTrack *tracks, int count,
                  char *out, size_t out_size) {
	char id[7];
	memcpy(id, game_id, 6);
	id[6] = 0;
	id[3] = '_';
	char file[64];
	snprintf(file, sizeof(file), EXPORT_DIR "/%s.json", id);
	if (out)
		snprintf(out, out_size, "%s", file);

	json_t *root = json_object();
	if (title && title[0])
		json_object_set_new(root, "title", json_string(title));
	json_t *arr = json_array();
	for (int i = 0; i < count; i++) {
		const ExportTrack *t = &tracks[i];
		json_t *o = json_object();
		json_object_set_new(o, "path", json_string(t->path));
		json_object_set_new(o, "name", json_string(t->name ? t->name : ""));
		if (t->hidden)
			json_object_set_new(o, "hidden", json_true());
		if (t->seconds > 0) {
			char len[16];
			int s = (int)(t->seconds + 0.5f);
			snprintf(len, sizeof(len), "%d:%02d", s / 60, s % 60);
			json_object_set_new(o, "length", json_string(len));
		}
		if (t->loops)
			json_object_set_new(o, "loops", json_true());
		json_array_append_new(arr, o);
	}
	json_object_set_new(root, "tracks", arr);
	bool ok = json_save(file, root);
	json_decref(root);
	return ok;
}
