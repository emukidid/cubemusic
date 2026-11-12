/* disc_sf.h -- vgmstream file access straight from the disc
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __DISC_SF_H
#define __DISC_SF_H

#include <gccore.h>
#include "libvgmstream.h"

/* Opens a disc file (full path, as in the file table) for vgmstream, with
 * read caching. Companion files vgmstream asks for are found the same way.
 * Close with libstreamfile_close(). NULL if the file isn't on the disc. */
libstreamfile_t *disc_sf_open(const char *path);

/* Opens a file on the SD card for vgmstream (stdio), counting what's read
 * in sd_bytes_read (for the SD light). Close with libstreamfile_close(). */
libstreamfile_t *sd_sf_open(const char *path);
extern volatile u32 sd_bytes_read;

#endif
