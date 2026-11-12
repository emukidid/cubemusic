/* backup.c -- copies tracks from the disc to SD on a worker thread
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <gccore.h>
#include "backup.h"
#include "banner.h"
#include "dvd.h"
#include "trackinfo.h"

#define COPY_CHUNK (256 * 1024)

BackupStatus backup_status;

static TrackList *job_list;
static const DiscFile **jobs;
static int job_count;
static lwp_t worker = LWP_THREAD_NULL;

void backup_path(char *out, size_t size, const char *game_id, const char *disc_path) {
	while (*disc_path == '/')
		disc_path++;
	snprintf(out, size, BACKUP_DIR "/%s/%s", game_id, disc_path);
}

bool mkdir_parents(const char *path) {
	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = 0;
		if (mkdir(tmp, 0777) < 0 && errno != EEXIST) {
			*p = '/';
			return false;
		}
		*p = '/';
	}
	return true;
}

/* same directory and name, ignoring the extension */
static bool same_stem(const char *a, const char *b) {
	const char *da = strrchr(a, '.'), *db = strrchr(b, '.');
	size_t la = da ? (size_t)(da - a) : strlen(a);
	size_t lb = db ? (size_t)(db - b) : strlen(b);
	return la == lb && !strncasecmp(a, b, la);
}

static void add_job(const DiscFile *f) {
	for (int i = 0; i < job_count; i++)
		if (jobs[i] == f)
			return;
	jobs[job_count++] = f;
}

static bool copy_file(const DiscFile *f, u8 *buf) {
	char out[1024];
	backup_path(out, sizeof(out), job_list->game_id, f->path);

	struct stat st;
	if (stat(out, &st) == 0 && st.st_size == f->size) {
		backup_status.skipped++;
		backup_status.bytes_done += f->size;
		return true;
	}
	if (!mkdir_parents(out)) {
		snprintf(backup_status.error, sizeof(backup_status.error), "Couldn't create folders for %s", f->path);
		return false;
	}
	FILE *fp = fopen(out, "wb");
	if (!fp) {
		snprintf(backup_status.error, sizeof(backup_status.error), "Couldn't create %s", out);
		return false;
	}
	bool ok = true;
	for (u32 done = 0; done < f->size && ok; ) {
		u32 n = f->size - done > COPY_CHUNK ? COPY_CHUNK : f->size - done;
		if (dvd_read(buf, f->offset + done, n) < 0) {
			snprintf(backup_status.error, sizeof(backup_status.error), "Disc read error in %s", f->path);
			ok = false;
		} else if (fwrite(buf, 1, n, fp) != n) {
			snprintf(backup_status.error, sizeof(backup_status.error), "SD write error (card full?)");
			ok = false;
		} else {
			backup_status.bytes_written += n;
		}
		done += n;
		backup_status.bytes_done += n;
		if (backup_status.cancel)
			ok = false;
	}
	fclose(fp);
	if (!ok)
		remove(out);   /* never leave a partial file that would later be "skipped" */
	return ok;
}

static bool job_copied(const DiscFile *f, const bool *copied) {
	for (int i = 0; i < job_count; i++)
		if (jobs[i] == f)
			return copied[i];
	return false;
}

/* Reads the header of each backed-up track (companions are on SD by now)
 * so the saved-tracks browser knows lengths without opening files. */
static void record_info(const bool *copied) {
	for (int i = 0; i < job_list->count; i++) {
		const Track *t = &job_list->tracks[i];
		TrackInfo ti;
		if (!t->selected || !t->file || t->companion || !job_copied(t->file, copied) ||
		    info_get(&job_list->info, t->key, &ti))
			continue;
		char out[1024];
		backup_path(out, sizeof(out), job_list->game_id, t->file->path);
		libstreamfile_t *sf = libstreamfile_open_from_stdio(out);
		info_probe(&job_list->info, t->key, sf, t->subsong);
		if (sf)
			libstreamfile_close(sf);
	}
	info_save(&job_list->info);
}

static void *backup_main(void *arg) {
	u8 *buf = memalign(32, COPY_CHUNK);
	bool *copied = calloc(job_count, sizeof(bool));
	for (int i = 0; i < job_count && buf && copied && !backup_status.cancel; i++) {
		const DiscFile *f = jobs[i];
		const char *base = strrchr(f->path, '/');
		snprintf(backup_status.current, sizeof(backup_status.current), "%s", base ? base + 1 : f->path);
		backup_status.file_index = i + 1;
		copied[i] = copy_file(f, buf);
		if (!copied[i] && !backup_status.cancel)
			backup_status.errors++;
	}
	free(buf);
	if (copied) {
		snprintf(backup_status.current, sizeof(backup_status.current), "Reading track lengths...");
		record_info(copied);
	}
	free(copied);
	free(jobs);
	jobs = NULL;
	backup_status.running = false;
	return NULL;
}

bool backup_start(TrackList *list) {
	if (backup_status.running)
		return false;
	if (worker != LWP_THREAD_NULL) {
		LWP_JoinThread(worker, NULL);
		worker = LWP_THREAD_NULL;
	}

	jobs = calloc(list->count + 1, sizeof(DiscFile *));
	if (!jobs)
		return false;
	job_count = 0;
	job_list = list;

	bool banks = false;
	for (int i = 0; i < list->count; i++) {
		Track *t = &list->tracks[i];
		if (!t->selected || !t->file)
			continue;
		add_job(t->file);
		/* a stream of a bank: the whole file is copied once, and the names
		 * file records which of its streams were backed up (the saved-tracks
		 * list shows those) */
		if (t->subsong) {
			names_add(&list->names, t->key);
			banks = true;
		}
		/* the files that go with it: Foo.spt -> Foo.spd, FooL.dsp -> FooR.dsp, ... */
		for (int j = 0; j < list->count; j++) {
			Track *c = &list->tracks[j];
			if (c->companion && c->file && (c->pair_left == t || same_stem(c->path, t->path)))
				add_job(c->file);
		}
	}
	if (!job_count) {
		free(jobs);
		jobs = NULL;
		return false;
	}
	const DiscFile *bnr = fst_find(BANNER_FILE);
	if (bnr)
		add_job(bnr);
	if (banks)
		names_save(&list->names);

	memset(&backup_status, 0, sizeof(backup_status));
	backup_status.file_total = job_count;
	for (int i = 0; i < list->count; i++)
		if (list->tracks[i].selected && list->tracks[i].file)
			backup_status.track_total++;
	for (int i = 0; i < job_count; i++)
		backup_status.bytes_total += jobs[i]->size;
	backup_status.running = true;
	/* below the audio decoder, above the UI */
	if (LWP_CreateThread(&worker, backup_main, NULL, NULL, 32 * 1024, 70) < 0) {
		backup_status.running = false;
		free(jobs);
		jobs = NULL;
		return false;
	}
	return true;
}
