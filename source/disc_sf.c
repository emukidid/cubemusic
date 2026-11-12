/* disc_sf.c -- vgmstream file access straight from the disc
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdlib.h>
#include <string.h>
#include "disc_sf.h"
#include "gcm_fst.h"
#include "dvd.h"

typedef struct {
	const DiscFile *file;
} DiscSF;

static libstreamfile_t *disc_sf_open_raw(const char *path);

static int dsf_read(void *user_data, uint8_t *dst, int64_t offset, int length) {
	DiscSF *d = user_data;
	if (offset < 0 || length <= 0 || offset >= d->file->size)
		return 0;
	if (offset + length > d->file->size)
		length = d->file->size - offset;
	if (dvd_read(dst, d->file->offset + (u32)offset, length) < 0)
		return 0;
	return length;
}

static int64_t dsf_get_size(void *user_data) {
	return ((DiscSF *)user_data)->file->size;
}

static const char *dsf_get_name(void *user_data) {
	return ((DiscSF *)user_data)->file->path;
}

static libstreamfile_t *dsf_open(void *user_data, const char *filename) {
	(void)user_data;
	return disc_sf_open_raw(filename);
}

static void dsf_close(libstreamfile_t *libsf) {
	if (!libsf)
		return;
	free(libsf->user_data);
	free(libsf);
}

static libstreamfile_t *disc_sf_open_raw(const char *path) {
	const DiscFile *f = fst_find(path);
	if (!f)
		return NULL;
	libstreamfile_t *sf = calloc(1, sizeof(libstreamfile_t));
	DiscSF *d = calloc(1, sizeof(DiscSF));
	if (!sf || !d) {
		free(sf);
		free(d);
		return NULL;
	}
	d->file = f;
	sf->user_data = d;
	sf->read = dsf_read;
	sf->get_size = dsf_get_size;
	sf->get_name = dsf_get_name;
	sf->open = dsf_open;
	sf->close = dsf_close;
	return sf;
}

libstreamfile_t *disc_sf_open(const char *path) {
	libstreamfile_t *raw = disc_sf_open_raw(path);
	if (!raw)
		return NULL;
	libstreamfile_t *buffered = libstreamfile_open_buffered(raw);
	if (!buffered) {
		libstreamfile_close(raw);
		return NULL;
	}
	return buffered;
}

/* ---------------------------------------------------------------- SD card */

volatile u32 sd_bytes_read;

static libstreamfile_t *sd_wrap(libstreamfile_t *inner);

static int ssf_read(void *user_data, uint8_t *dst, int64_t offset, int length) {
	libstreamfile_t *in = user_data;
	int n = in->read(in->user_data, dst, offset, length);
	if (n > 0)
		sd_bytes_read += n;
	return n;
}

static int64_t ssf_get_size(void *user_data) {
	libstreamfile_t *in = user_data;
	return in->get_size(in->user_data);
}

static const char *ssf_get_name(void *user_data) {
	libstreamfile_t *in = user_data;
	return in->get_name(in->user_data);
}

static libstreamfile_t *ssf_open(void *user_data, const char *filename) {
	libstreamfile_t *in = user_data;
	return sd_wrap(in->open(in->user_data, filename));
}

static void ssf_close(libstreamfile_t *libsf) {
	if (!libsf)
		return;
	libstreamfile_close(libsf->user_data);
	free(libsf);
}

static libstreamfile_t *sd_wrap(libstreamfile_t *inner) {
	if (!inner)
		return NULL;
	libstreamfile_t *sf = calloc(1, sizeof(libstreamfile_t));
	if (!sf) {
		libstreamfile_close(inner);
		return NULL;
	}
	sf->user_data = inner;
	sf->read = ssf_read;
	sf->get_size = ssf_get_size;
	sf->get_name = ssf_get_name;
	sf->open = ssf_open;
	sf->close = ssf_close;
	return sf;
}

libstreamfile_t *sd_sf_open(const char *path) {
	return sd_wrap(libstreamfile_open_from_stdio(path));
}
