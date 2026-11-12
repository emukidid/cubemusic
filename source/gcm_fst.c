/* gcm_fst.c -- the disc's file table, indexed by full path
 *
 * Copyright (C) 2016 emu_kidid
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <malloc.h>
#include <gccore.h>
#include "gcm_fst.h"
#include "dvd.h"

#define FST_ENTRY_SIZE 12
#define FST_MAX_DEPTH  32
#define FST_MAX_SIZE   (8 * 1024 * 1024)

static DiscFile *files;      /* sorted by full path */
static DiscFile **by_name;   /* the same files, sorted by file name (no folders) */
static int file_count;
static char *path_pool;      /* every path, back to back */
static void *afs_pools;      /* paths of files inside AFS archives; a list, first word = next */

void fst_free(void) {
	free(files);
	free(by_name);
	free(path_pool);
	while (afs_pools) {
		void *next = *(void **)afs_pools;
		free(afs_pools);
		afs_pools = next;
	}
	files = NULL;
	by_name = NULL;
	path_pool = NULL;
	file_count = 0;
}

static const char *base_name(const char *path) {
	const char *s = strrchr(path, '/');
	const char *b = strrchr(path, '\\');
	if (b > s)
		s = b;
	return s ? s + 1 : path;
}

/* strcasecmp treating '\\' as '/' */
static int path_cmp(const char *a, const char *b) {
	for (;; a++, b++) {
		int ca = *a == '\\' ? '/' : tolower((unsigned char)*a);
		int cb = *b == '\\' ? '/' : tolower((unsigned char)*b);
		if (ca != cb || !ca)
			return ca - cb;
	}
}

static int file_cmp(const void *a, const void *b) {
	return path_cmp(((const DiscFile *)a)->path, ((const DiscFile *)b)->path);
}

static int name_cmp(const void *a, const void *b) {
	return path_cmp(base_name((*(DiscFile *const *)a)->path), base_name((*(DiscFile *const *)b)->path));
}

static u32 be32(const u8 *p) {
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static u32 le32(const u8 *p) {
	return ((u32)p[3] << 24) | ((u32)p[2] << 16) | ((u32)p[1] << 8) | p[0];
}

#define AFS_MAX_FILES 16384
#define AFS_NAME_LEN  32
#define AFS_NAME_ENTRY 48

static bool ends_with_ci(const char *s, const char *end) {
	size_t sl = strlen(s), el = strlen(end);
	return sl >= el && !strcasecmp(s + sl - el, end);
}

static bool afs_name_ok(const char *n) {
	if (!n[0] || !strcmp(n, ".") || !strcmp(n, ".."))
		return false;
	for (; *n; n++)
		if ((u8)*n < 0x20 || strchr("/\\:*?\"<>|", *n))
			return false;
	return true;
}

/* CRI AFS archives ("AFS", a count, then offset/size pairs, then usually a
 * table of 48-byte entries starting with a 32-byte name) hold many games'
 * music as .adx files. List each file inside as "archive.afs/name", so it can
 * be found, played and backed up like any other disc file. Appends to *list;
 * returns false only when out of memory. */
static bool afs_expand(const DiscFile *afs, DiscFile **list, int *count, int *cap) {
	u8 head[8];
	if (afs->size < 16 || dvd_read(head, afs->offset, 8) < 0 || memcmp(head, "AFS", 4))
		return true;
	u32 n = le32(head + 4);
	u32 tlen = 8 + n * 8 + 8;
	if (!n || n > AFS_MAX_FILES || tlen > afs->size)
		return true;
	u8 *tab = malloc(tlen);
	if (!tab)
		return false;
	if (dvd_read(tab, afs->offset, tlen) < 0) {
		free(tab);
		return true;
	}

	/* The name table's position follows the file table, or else sits just
	 * before the first file. */
	u32 noff = le32(tab + 8 + n * 8);
	u32 first = le32(tab + 8);
	u8 tmp[8];
	if (!noff && first >= tlen + 8 && first <= afs->size &&
	    dvd_read(tmp, afs->offset + first - 8, 8) >= 0)
		noff = le32(tmp);
	u8 *nt = NULL;
	if (noff && noff < afs->size && n * AFS_NAME_ENTRY <= afs->size - noff) {
		nt = malloc(n * AFS_NAME_ENTRY);
		if (nt && dvd_read(nt, afs->offset + noff, n * AFS_NAME_ENTRY) < 0) {
			free(nt);
			nt = NULL;
		}
	}

	size_t plen = strlen(afs->path);
	size_t each = plen + 1 + AFS_NAME_LEN + 1;
	char *pool = malloc(sizeof(void *) + n * each);
	if (*count + (int)n > *cap) {
		int ncap = *count + n + 64;
		DiscFile *grown = realloc(*list, ncap * sizeof(DiscFile));
		if (grown) {
			*list = grown;
			*cap = ncap;
		}
	}
	if (!pool || *count + (int)n > *cap) {
		free(pool);
		free(nt);
		free(tab);
		return false;
	}
	*(void **)pool = afs_pools;
	afs_pools = pool;
	char *p = pool + sizeof(void *);

	int added = 0;
	for (u32 i = 0; i < n; i++) {
		u32 off = le32(tab + 8 + i * 8), size = le32(tab + 12 + i * 8);
		if (!size || off >= afs->size || size > afs->size - off)
			continue;
		char name[AFS_NAME_LEN + 1];
		if (nt) {
			memcpy(name, nt + i * AFS_NAME_ENTRY, AFS_NAME_LEN);
			name[AFS_NAME_LEN] = 0;
		}
		if (!nt || !afs_name_ok(name))
			snprintf(name, sizeof(name), "%05u.adx", (unsigned)i);   /* AFS is CRI's ADX container */
		DiscFile *f = &(*list)[(*count)++];
		f->path = p;
		p += sprintf(p, "%s/%s", afs->path, name) + 1;
		f->offset = afs->offset + off;
		f->size = size;
		added++;
	}
	printf("fst: %s holds %d files%s\r\n", afs->path, added, nt ? "" : " (no names)");
	free(nt);
	free(tab);
	return true;
}

int fst_load(char *game_name) {
	static DiskHeader header;
	fst_free();

	if (dvd_read(&header, 0, sizeof(DiskHeader)) < 0) {
		printf("fst: header read failed\r\n");
		return -1;
	}
	if (game_name) {
		memcpy(game_name, header.GameName, 64);
		game_name[63] = 0;
	}

	/* (a real file table is at most a few hundred KB; anything else means this
	 * isn't a GameCube disc, or it's not the disc that's being read) */
	if (header.FSTSize < FST_ENTRY_SIZE || header.FSTSize > FST_MAX_SIZE) {
		printf("fst: implausible table size %u\r\n", header.FSTSize);
		return -1;
	}
	u8 *fst = malloc(header.FSTSize + 1);
	if (!fst)
		return -1;
	if (dvd_read(fst, header.FSTOffset, header.FSTSize) < 0) {
		printf("fst: table read failed (%u bytes at %08X)\r\n", header.FSTSize, header.FSTOffset);
		free(fst);
		return -1;
	}

	fst[header.FSTSize] = 0;   /* names can't run off the end */
	u32 entries = be32(fst + 8);
	if (entries < 1 || entries > header.FSTSize / FST_ENTRY_SIZE) {
		printf("fst: implausible entry count %u\r\n", entries);
		free(fst);
		return -1;
	}
	const char *names = (const char *)fst + entries * FST_ENTRY_SIZE;
	u32 names_size = header.FSTSize - entries * FST_ENTRY_SIZE;
	for (u32 i = 1; i < entries; i++) {
		if ((be32(fst + i * FST_ENTRY_SIZE) & 0xFFFFFF) >= names_size) {
			printf("fst: entry %u's name is outside the table\r\n", i);
			free(fst);
			return -1;
		}
	}

	files = calloc(entries, sizeof(DiscFile));
	/* Paths repeat their directory names, so size the pool by walking first. */
	u32 pool_need = 0;
	{
		u32 dir_end[FST_MAX_DEPTH];
		u32 dir_len[FST_MAX_DEPTH];
		int depth = 0;
		u32 cur_len = 0;
		for (u32 i = 1; i < entries; i++) {
			while (depth && i >= dir_end[depth - 1])
				cur_len = dir_len[--depth];
			const u8 *e = fst + i * FST_ENTRY_SIZE;
			u32 nlen = strlen(names + (be32(e) & 0xFFFFFF));
			if (e[0]) {
				if (depth < FST_MAX_DEPTH) {
					dir_end[depth] = be32(e + 8);
					dir_len[depth++] = cur_len;
					cur_len += nlen + 1;
				}
			} else {
				pool_need += cur_len + nlen + 1;
			}
		}
	}
	path_pool = malloc(pool_need + 1);
	if (!files || !path_pool) {
		free(fst);
		fst_free();
		return -1;
	}

	/* Walk again, building "dir/dir/name" for every file. */
	char cur[1024];
	u32 dir_end[FST_MAX_DEPTH];
	u32 dir_len[FST_MAX_DEPTH];
	int depth = 0;
	u32 cur_len = 0;
	char *pool = path_pool;
	cur[0] = 0;
	for (u32 i = 1; i < entries; i++) {
		while (depth && i >= dir_end[depth - 1]) {
			cur_len = dir_len[--depth];
			cur[cur_len] = 0;
		}
		const u8 *e = fst + i * FST_ENTRY_SIZE;
		const char *name = names + (be32(e) & 0xFFFFFF);
		u32 nlen = strlen(name);
		if (e[0]) {
			if (depth < FST_MAX_DEPTH && cur_len + nlen + 2 < sizeof(cur)) {
				dir_end[depth] = be32(e + 8);
				dir_len[depth++] = cur_len;
				memcpy(cur + cur_len, name, nlen);
				cur_len += nlen;
				cur[cur_len++] = '/';
				cur[cur_len] = 0;
			}
		} else {
			DiscFile *f = &files[file_count++];
			f->path = pool;
			memcpy(pool, cur, cur_len);
			memcpy(pool + cur_len, name, nlen + 1);
			pool += cur_len + nlen + 1;
			f->offset = be32(e + 4);
			f->size = be32(e + 8);
		}
	}
	free(fst);

	/* Expand AFS archives (only those on the disc proper, not ones just added). */
	int disc_files = file_count, cap = file_count;
	for (int i = 0; i < disc_files; i++) {
		if (!ends_with_ci(files[i].path, ".afs"))
			continue;
		DiscFile afs = files[i];   /* files may move */
		if (!afs_expand(&afs, &files, &file_count, &cap))
			break;
	}

	qsort(files, file_count, sizeof(DiscFile), file_cmp);
	by_name = malloc(file_count * sizeof(DiscFile *));
	if (by_name) {
		for (int i = 0; i < file_count; i++)
			by_name[i] = &files[i];
		qsort(by_name, file_count, sizeof(DiscFile *), name_cmp);
	}
	printf("fst: %d files indexed\r\n", file_count);
	return file_count;
}

int fst_count(void) {
	return file_count;
}

const DiscFile *fst_file(int index) {
	return index >= 0 && index < file_count ? &files[index] : NULL;
}

static int path_depth(const char *p) {
	int d = 0;
	for (; *p; p++)
		d += (*p == '/');
	return d;
}

/* path_cmp on the last len characters of full against tail, which must
 * start at a folder boundary in full. */
static bool tail_matches(const char *full, const char *tail) {
	size_t fl = strlen(full), tl = strlen(tail);
	if (tl > fl)
		return false;
	if (tl < fl && full[fl - tl - 1] != '/')
		return false;
	return !path_cmp(full + fl - tl, tail);
}

const DiscFile *fst_find(const char *path) {
	while (*path == '/' || *path == '\\')
		path++;
	if (path[0] == '.' && (path[1] == '/' || path[1] == '\\'))
		path += 2;

	/* exact full path */
	int lo = 0, hi = file_count - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		int c = path_cmp(path, files[mid].path);
		if (!c)
			return &files[mid];
		if (c < 0)
			hi = mid - 1;
		else
			lo = mid + 1;
	}
	if (!by_name)
		return NULL;

	/* Rips often list paths relative to some folder ("rui_samusL.dsp" for
	 * "Audio/rui_samusL.dsp"): match the path as the tail of a disc path.
	 * Find the run of files with the same file name, then check the tail. */
	const char *name = base_name(path);
	lo = 0;
	hi = file_count;
	while (lo < hi) {   /* first file whose name >= name */
		int mid = (lo + hi) / 2;
		if (path_cmp(base_name(by_name[mid]->path), name) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	const DiscFile *best = NULL;
	int matches = 0;
	for (int i = lo; i < file_count && !path_cmp(base_name(by_name[i]->path), name); i++) {
		if (!tail_matches(by_name[i]->path, path))
			continue;
		matches++;
		if (!best || path_depth(by_name[i]->path) < path_depth(best->path))
			best = by_name[i];
	}
	if (matches > 1)
		printf("fst: %s matches %d files, using %s\r\n", path, matches, best->path);
	return best;
}
