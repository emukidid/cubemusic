/* dvd.c -- low level disc drive access (original GameCube discs only)
 *
 * Talks to the drive interface (DI) registers directly. The DI interrupt
 * masks are left off for every command so libogc's own DI handler never
 * sees our transfers; completion is polled, sleeping between polls so other
 * threads (audio decode, UI) keep running while a read is in flight.
 *
 * Copyright (C) 2016 emu_kidid
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <gccore.h>
#include <ogc/mutex.h>
#include "dvd.h"

#define DI_SR      0   /* status: write 1 to clear DEINT(bit 2)/TCINT(4)/BRKINT(6) */
#define DI_CVR     1
#define DI_CMDBUF0 2
#define DI_CMDBUF1 3
#define DI_CMDBUF2 4
#define DI_MAR     5
#define DI_LENGTH  6
#define DI_CR      7
#define DI_IMMBUF  8

#define DI_SR_CLEAR_ALL_MASKS_OFF 0x54   /* clear the three flags, all interrupt masks off */
#define DI_SR_DEINT               0x04

/* Reads go through this bounce buffer so callers needn't care about the
 * DI's alignment rules (32-byte buffer and length, 4-byte offset). */
#define BOUNCE_SIZE (64 * 1024)
static u8 bounce[BOUNCE_SIZE] __attribute__((aligned(32)));

static volatile u32 *const di = (volatile u32 *)0xCC006000;
static mutex_t dvd_lock = LWP_MUTEX_NULL;

void dvd_init(void) {
	if (dvd_lock == LWP_MUTEX_NULL)
		LWP_MutexInit(&dvd_lock, false);
}

/* Starts the command already in the registers and waits for it. */
static int di_run(u32 cr) {
	di[DI_CR] = cr;
	while (di[DI_CR] & 1)
		usleep(250);
	return (di[DI_SR] & DI_SR_DEINT) ? -1 : 0;
}

/* The drive's release date, from an Inquiry (0 if it fails). */
static u32 di_inquiry_date(void) {
	static u8 info[32] __attribute__((aligned(32)));
	DCInvalidateRange(info, sizeof(info));
	di[DI_SR] = DI_SR_CLEAR_ALL_MASKS_OFF;
	di[DI_CVR] = 0;
	di[DI_CMDBUF0] = 0x12000000;
	di[DI_CMDBUF1] = 0;
	di[DI_CMDBUF2] = sizeof(info);
	di[DI_MAR] = MEM_VIRTUAL_TO_PHYSICAL(info);
	di[DI_LENGTH] = sizeof(info);
	if (di_run(3) < 0)
		return 0;
	DCInvalidateRange(info, sizeof(info));
	return ((u32)info[4] << 24) | ((u32)info[5] << 16) | ((u32)info[6] << 8) | info[7];
}

#define FLIPPY_DATE_A 0x20220420
#define FLIPPY_DATE_B 0x20220426

static bool flippy_bypassed;   /* we switched it, so switch it back on exit */

static void flippy_bypass(void) {
	u32 date = di_inquiry_date();
	if (date != FLIPPY_DATE_A && date != FLIPPY_DATE_B)
		return;
	flippy_bypassed = true;
	printf("dvd: FlippyDrive found (%08X), switching it to pass-through\r\n", date);
	di[DI_SR] = DI_SR_CLEAR_ALL_MASKS_OFF;
	di[DI_CVR] = 0;
	di[DI_CMDBUF0] = 0xDC000000;
	di[DI_CMDBUF1] = 0;
	di[DI_CMDBUF2] = 0;
	di_run(1);
	usleep(50 * 1000);
	di[DI_SR] = DI_SR_CLEAR_ALL_MASKS_OFF;
	di[DI_CVR] = 2;      /* (clear any cover interrupt from the switch) */
}

void dvd_flippy_restore(void) {
	LWP_MutexLock(dvd_lock);
	if (flippy_bypassed) {
		printf("dvd: switching the FlippyDrive back from pass-through\r\n");
		di[DI_SR] = DI_SR_CLEAR_ALL_MASKS_OFF;
		di[DI_CVR] = 0;
		di[DI_CMDBUF0] = 0xDC000000;
		di[DI_CMDBUF1] = 0xE3F72BAB;
		di[DI_CMDBUF2] = 0x72648977;
		di_run(1);
		usleep(50 * 1000);
		di[DI_SR] = DI_SR_CLEAR_ALL_MASKS_OFF;
		di[DI_CVR] = 2;
		flippy_bypassed = false;
	}
	LWP_MutexUnlock(dvd_lock);
}

void dvd_reset(void) {
	LWP_MutexLock(dvd_lock);
	flippy_bypass();
	di[DI_CVR] = 2;
	u32 v = *(volatile u32 *)0xCC003024;
	*(volatile u32 *)0xCC003024 = (v & ~4) | 1;
	sleep(1);
	*(volatile u32 *)0xCC003024 = v | 5;
	LWP_MutexUnlock(dvd_lock);
}

int dvd_read_id(dvddiskid *id) {
	static u8 idbuf[32] __attribute__((aligned(32)));
	LWP_MutexLock(dvd_lock);
	DCInvalidateRange(idbuf, sizeof(idbuf));
	di[DI_SR] = DI_SR_CLEAR_ALL_MASKS_OFF;
	di[DI_CVR] = 0;
	di[DI_CMDBUF0] = 0xA8000040;
	di[DI_CMDBUF1] = 0;
	di[DI_CMDBUF2] = sizeof(idbuf);
	di[DI_MAR] = MEM_VIRTUAL_TO_PHYSICAL(idbuf);
	di[DI_LENGTH] = sizeof(idbuf);
	int ret = di_run(3);
	DCInvalidateRange(idbuf, sizeof(idbuf));
	memcpy(id, idbuf, sizeof(dvddiskid));
	LWP_MutexUnlock(dvd_lock);
	return ret;
}

/* One DMA read of len bytes (multiple of 32) at offset (multiple of 4) into bounce. */
static int di_read_raw(u32 offset, u32 len) {
	DCInvalidateRange(bounce, len);
	di[DI_SR] = DI_SR_CLEAR_ALL_MASKS_OFF;
	di[DI_CVR] = 0;
	di[DI_CMDBUF0] = 0xA8000000;
	di[DI_CMDBUF1] = offset >> 2;
	di[DI_CMDBUF2] = len;
	di[DI_MAR] = MEM_VIRTUAL_TO_PHYSICAL(bounce);
	di[DI_LENGTH] = len;
	int ret = di_run(3);
	DCInvalidateRange(bounce, len);
	return ret;
}

volatile u32 dvd_bytes_read;

s32 dvd_read(void *dst, u32 offset, u32 len) {
	u8 *out = dst;
	u32 left = len;
	int err = 0;

	LWP_MutexLock(dvd_lock);
	while (left && !err) {
		u32 start = offset & ~31;           /* read from a 32-byte boundary */
		u32 skip = offset - start;
		u32 want = skip + left;
		if (want > BOUNCE_SIZE) want = BOUNCE_SIZE;
		u32 rlen = (want + 31) & ~31;
		if (rlen > BOUNCE_SIZE) rlen = BOUNCE_SIZE;

		err = di_read_raw(start, rlen);
		dvd_bytes_read += rlen;
		u32 got = rlen - skip;
		if (got > left) got = left;
		memcpy(out, bounce + skip, got);
		out += got;
		offset += got;
		left -= got;
	}
	LWP_MutexUnlock(dvd_lock);
	return err ? -1 : (s32)len;
}

void dvd_motor_off(void) {
	LWP_MutexLock(dvd_lock);
	di[DI_SR] = DI_SR_CLEAR_ALL_MASKS_OFF;
	di[DI_CVR] = 0;
	di[DI_CMDBUF0] = 0xE3000000;
	di[DI_CMDBUF1] = 0;
	di[DI_CMDBUF2] = 0;
	di_run(1);
	LWP_MutexUnlock(dvd_lock);
}
