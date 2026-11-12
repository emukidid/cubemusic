/* saved.c -- browsing tracks already backed up to SD
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "saved.h"
#include "backup.h"
#include "tracks.h"

int saved_list_games(SavedGame **out) {
	*out = NULL;
	DIR *d = opendir(BACKUP_DIR);
	if (!d)
		return 0;
	int count = 0, cap = 0;
	SavedGame *games = NULL;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_type != DT_DIR || strlen(e->d_name) != 6)
			continue;
		if (count == cap) {
			cap = cap ? cap * 2 : 32;
			SavedGame *g = realloc(games, cap * sizeof(SavedGame));
			if (!g)
				break;
			games = g;
		}
		SavedGame *g = &games[count++];
		snprintf(g->game_id, sizeof(g->game_id), "%s", e->d_name);
		char bnr[300];
		snprintf(bnr, sizeof(bnr), BACKUP_DIR "/%s/" BANNER_FILE, g->game_id);
		static Banner b;
		NameTable names = {0};
		names_load(&names, g->game_id);
		if (names.title[0])
			snprintf(g->title, sizeof(g->title), "%s", names.title);
		else if (banner_load_file(&b, bnr) && b.title[0])
			snprintf(g->title, sizeof(g->title), "%s", b.title);
		else
			snprintf(g->title, sizeof(g->title), "%s", g->game_id);
		names_free(&names);
		/* added: the folder's creation time (FAT keeps one), else its last change */
		char dir[300];
		struct stat st;
		snprintf(dir, sizeof(dir), BACKUP_DIR "/%s", g->game_id);
		g->added = stat(dir, &st) == 0 ? (long long)(st.st_ctime ? st.st_ctime : st.st_mtime) : 0;
	}
	closedir(d);
	*out = games;
	return count;
}

void saved_free_games(SavedGame *games) {
	free(games);
}

typedef struct {
	SavedTrack *items;
	int count, cap;
	const NameTable *names;
} TrackVec;

static void add_track(TrackVec *v, const char *path, const char *name) {
	if (v->count == v->cap) {
		int cap = v->cap ? v->cap * 2 : 64;
		SavedTrack *n = realloc(v->items, cap * sizeof(SavedTrack));
		if (!n)
			return;
		v->items = n;
		v->cap = cap;
	}
	SavedTrack *t = &v->items[v->count++];
	memset(t, 0, sizeof(*t));   /* (not ticked: realloc'd memory isn't cleared) */
	snprintf(t->path, sizeof(t->path), "%s", path);
	if (name)
		snprintf(t->name, sizeof(t->name), "%s", name);
	else
		tracks_default_name(t->name, sizeof(t->name), path);
}

static void walk(TrackVec *v, const char *root, const char *rel) {
	char dir[512];
	snprintf(dir, sizeof(dir), "%s%s%s", root, *rel ? "/" : "", rel);
	DIR *d = opendir(dir);
	if (!d)
		return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		char path[256];
		snprintf(path, sizeof(path), "%s%s%s", rel, *rel ? "/" : "", e->d_name);
		if (e->d_type == DT_DIR) {
			walk(v, root, path);
			continue;
		}
		if (tracks_is_companion(path))
			continue;
		/* a bank: list the streams backed up from it (the names file has
		 * "<path>#<n>" for each), not the file */
		bool bank = false;
		size_t plen = strlen(path);
		for (int i = 0; v->names && i < v->names->count; i++) {
			const NameEntry *be = &v->names->entries[i];
			if (strncasecmp(be->path, path, plen) || be->path[plen] != '#')
				continue;
			bank = true;
			if (!be->hidden)
				add_track(v, be->path, be->name);
		}
		if (bank)
			continue;
		const NameEntry *ne = v->names ? names_find(v->names, path) : NULL;
		if (ne && ne->hidden)
			continue;
		add_track(v, path, ne ? ne->name : NULL);
	}
	closedir(d);
}

/* by path, a bank's streams in number order */
static int track_cmp(const void *a, const void *b) {
	char pa[256], pb[256];
	int sa = tracks_key_split(((const SavedTrack *)a)->path, pa, sizeof(pa));
	int sb = tracks_key_split(((const SavedTrack *)b)->path, pb, sizeof(pb));
	int c = strcasecmp(pa, pb);
	return c ? c : sa - sb;
}

static SavedTrack *find_track(TrackVec *v, const char *path) {
	for (int i = 0; i < v->count; i++)
		if (!strcasecmp(v->items[i].path, path))
			return &v->items[i];
	return NULL;
}

/* L/R stereo pairs: list the left half (vgmstream plays both), drop the right. */
static void join_pairs(TrackVec *v) {
	for (int i = 0; i < v->count; i++) {
		SavedTrack *l = &v->items[i];
		char right[256];
		if (!l->path[0] || !tracks_right_partner(l->path, right, sizeof(right)))
			continue;
		SavedTrack *r = find_track(v, right);
		if (!r || r == l)
			continue;
		r->path[0] = 0;   /* removed below */
		const NameEntry *ne = v->names ? names_find(v->names, l->path) : NULL;
		if (!ne || !ne->name)
			tracks_default_track_name(l->name, sizeof(l->name), l->path, true);
	}
	int n = 0;
	for (int i = 0; i < v->count; i++)
		if (v->items[i].path[0])
			v->items[n++] = v->items[i];
	v->count = n;
}

int saved_list_tracks(const char *game_id, const NameTable *names, SavedTrack **out) {
	char root[64];
	snprintf(root, sizeof(root), BACKUP_DIR "/%s", game_id);
	TrackVec v = { .names = names };
	walk(&v, root, "");
	join_pairs(&v);
	qsort(v.items, v.count, sizeof(SavedTrack), track_cmp);
	*out = v.items;
	return v.count;
}

void saved_free_tracks(SavedTrack *tracks) {
	free(tracks);
}

/* --------------------------------------------------------------- deleting */

static long long remove_file(const char *path) {
	struct stat st;
	if (stat(path, &st) != 0)
		return 0;
	return unlink(path) == 0 ? (long long)st.st_size : 0;
}

/* an empty folder (libfat's devoptab has rmdir; unlink as well, just in case) */
static int remove_dir(const char *path) {
	return rmdir(path) == 0 || unlink(path) == 0 ? 0 : -1;
}

/* removes the folders of path that are now empty, up to (not including) stop */
static void prune(const char *path, const char *stop) {
	char dir[1024];
	snprintf(dir, sizeof(dir), "%s", path);
	size_t keep = strlen(stop);
	char *slash;
	while ((slash = strrchr(dir, '/')) && (size_t)(slash - dir) > keep) {
		*slash = 0;
		DIR *d = opendir(dir);
		if (!d)
			return;
		bool empty = true;
		struct dirent *e;
		while (empty && (e = readdir(d)))
			if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
				empty = false;
		closedir(d);
		if (!empty || remove_dir(dir) != 0)
			return;
	}
}

/* a file and everything that goes with it */
static long long remove_with_companions(const char *game_dir, const char *rel) {
	char full[1024], dir[1024];
	snprintf(full, sizeof(full), "%s/%s", game_dir, rel);
	long long freed = 0;

	/* the other half of a stereo pair */
	char right[256];
	if (tracks_right_partner(rel, right, sizeof(right))) {
		char rfull[1024];
		snprintf(rfull, sizeof(rfull), "%s/%s", game_dir, right);
		freed += remove_file(rfull);
	}
	/* companions: the same name with a companion's extension, in the same folder */
	snprintf(dir, sizeof(dir), "%s", full);
	char *slash = strrchr(dir, '/');
	const char *base = slash + 1;
	*slash = 0;
	const char *dot = strrchr(base, '.');
	size_t stem = dot ? (size_t)(dot - base) : strlen(base);
	char stem_name[256];
	snprintf(stem_name, sizeof(stem_name), "%.*s", (int)stem, base);
	DIR *d = opendir(dir);
	if (d) {
		char victims[16][256];
		int nv = 0;
		struct dirent *e;
		while ((e = readdir(d)) && nv < 16) {
			const char *ed = strrchr(e->d_name, '.');
			if (!ed || (size_t)(ed - e->d_name) != stem || strncasecmp(e->d_name, stem_name, stem))
				continue;
			if (tracks_is_companion(e->d_name))
				snprintf(victims[nv++], 256, "%s", e->d_name);
		}
		closedir(d);
		for (int i = 0; i < nv; i++) {
			char c[1024];
			snprintf(c, sizeof(c), "%s/%s", dir, victims[i]);
			freed += remove_file(c);
		}
	}
	freed += remove_file(full);
	prune(full, game_dir);
	return freed;
}

long long saved_delete_track(const char *game_id, NameTable *names, const char *key) {
	char game_dir[300], file[256];
	snprintf(game_dir, sizeof(game_dir), BACKUP_DIR "/%s", game_id);
	int sub = tracks_key_split(key, file, sizeof(file));
	if (sub > 0) {
		/* a stream of a bank: forget it; the bank goes with the last one */
		names_remove(names, key);
		size_t plen = strlen(file);
		for (int i = 0; i < names->count; i++)
			if (!strncasecmp(names->entries[i].path, file, plen) && names->entries[i].path[plen] == '#')
				return 0;
	}
	return remove_with_companions(game_dir, file);
}

static long long remove_tree(const char *path) {
	long long freed = 0;
	DIR *d = opendir(path);
	if (!d)
		return remove_file(path);
	char (*names)[256] = NULL;
	int count = 0, cap = 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		if (count == cap) {
			int c = cap ? cap * 2 : 32;
			char (*n)[256] = realloc(names, c * 256);
			if (!n)
				break;
			names = n;
			cap = c;
		}
		snprintf(names[count++], 256, "%s", e->d_name);
	}
	closedir(d);
	for (int i = 0; i < count; i++) {
		char child[1024];
		snprintf(child, sizeof(child), "%s/%s", path, names[i]);
		freed += remove_tree(child);
	}
	free(names);
	remove_dir(path);
	return freed;
}

long long saved_delete_game(const char *game_id) {
	char game_dir[300];
	snprintf(game_dir, sizeof(game_dir), BACKUP_DIR "/%s", game_id);
	return remove_tree(game_dir);
}

bool saved_has_tracks(const char *game_id) {
	SavedTrack *t = NULL;
	int n = saved_list_tracks(game_id, NULL, &t);
	saved_free_tracks(t);
	return n > 0;
}
