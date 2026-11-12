/* dvd.h -- low level disc drive access (original GameCube discs only)
 *
 * Copyright (C) 2016 emu_kidid
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#ifndef __DVD_H
#define __DVD_H

#include <gccore.h>

/* Must be called once before anything else here (sets up the lock). */
void dvd_init(void);

/* Hard-resets the drive so it re-reads (and authenticates) the inserted disc. */
void dvd_reset(void);

/* Reads the 32-byte disc ID. Returns 0 on success. */
int dvd_read_id(dvddiskid *id);

/* Reads len bytes from any disc offset into any buffer (no alignment rules).
 * Thread safe: callers queue on a lock and sleep while the drive works.
 * Returns len on success, -1 on a drive error. */
s32 dvd_read(void *dst, u32 offset, u32 len);
/* bytes read from the disc so far (wraps; for the read-rate display) */
extern volatile u32 dvd_bytes_read;

/* Stops the disc spinning. */
void dvd_motor_off(void);

/* dvd_reset() puts a FlippyDrive into pass-through to the real drive; this
 * puts it back (on exit, so the next program sees it as it was). Does
 * nothing if there's no FlippyDrive. */
void dvd_flippy_restore(void);

#endif
