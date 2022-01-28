#include <stdio.h>
#include <gccore.h>		/*** Wrapper to include common libogc headers ***/
#include <ogcsys.h>		/*** Needed for console support ***/
#include <ogc/dvd.h>	
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <stdint.h>
#include "dvd.h"
#include "drivecodes.h"

/* Simple DVD functions */
u32 inquiryBuf[32] __attribute__((aligned(32)));
volatile unsigned long* dvd = (volatile unsigned long*)0xCC006000;

void dvd_reset()
{
	dvd[1] = 2;
	volatile unsigned long v = *(volatile unsigned long*)0xcc003024;
	*(volatile unsigned long*)0xcc003024 = (v & ~4) | 1;
	sleep(1);
	*(volatile unsigned long*)0xcc003024 = v | 5;
}

void dvd_set_streaming(char stream)
{
	if(stream)
	{
		dvd[0] = 0x2E;
		dvd[2] = 0xE4010000;
		dvd[7] = 1;
		while (dvd[7] & 1);
	}
	else
	{		
		dvd[0] = 0x2E;
		dvd[2] = 0xE4000000;
		dvd[7] = 1;
		while (dvd[7] & 1);
	}
}

unsigned int dvd_get_error(void)
{
	dvd[2] = 0xE0000000;
	dvd[8] = 0;
	dvd[7] = 1;
	while (dvd[7] & 1);
	return dvd[8];
}

void dvd_motor_off()
{
	dvd[0] = 0x2E;
	dvd[1] = 0;

	dvd[2] = 0xe3000000;
	dvd[3] = 0;
	dvd[4] = 0;
	dvd[5] = 0;
	dvd[6] = 0;
	dvd[7] = 1; // enable reading!
	while (dvd[7] & 1);
}


void dvd_setstatus()
{
	dvd[0] = 0x2E;
	dvd[1] = 0;

	dvd[2] = 0xee060300;
	dvd[3] = 0;
	dvd[4] = 0;
	dvd[5] = 0;
	dvd[6] = 0;
	dvd[7] = 1; // enable reading!
	while (dvd[7] & 1);
}

void dvd_setextension()
{
	dvd[0] = 0x2E;
	dvd[1] = 0;

	dvd[2] = 0x55010000;
	dvd[3] = 0;
	dvd[4] = 0;
	dvd[5] = 0;
	dvd[6] = 0;
	dvd[7] = 1; // enable reading!
	while (dvd[7] & 1);
}

void dvd_unlock()
{
	dvd[0] |= 0x00000014;
	dvd[1] = 0;
	dvd[2] = 0xFF014D41;
	dvd[3] = 0x54534849;	//MATS
	dvd[4] = 0x54410200;	//HITA
	dvd[7] = 1;
	while (dvd[7] & 1);
	
	dvd[0] |= 0x00000014;
	dvd[1] = 0;
	dvd[2] = 0xFF004456;
	dvd[3] = 0x442D4741;	//DVD-
	dvd[4] = 0x4D450300;	//GAME
	dvd[7] = 1;
	while (dvd[7] & 1);
}


u32 dvd_readmem_32(u32 addr)
{
	dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = 0xFE010000;	
	dvd[3] = addr;
	dvd[4] = 0x00010000;	
	dvd[8] = 0;
	dvd[7] = 1;

	while (dvd[7] & 1);
	return dvd[8];
}

int dvd_writemem_32(u32 addr, u32 dat)
{
	dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = 0xFE010100;	
	dvd[3] = addr;
	dvd[4] = 0x00040000;	
	dvd[5] = 0;
	dvd[6] = 0;
	dvd[7] = 3;
	while (dvd[7] & 1);

	dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = dat;
	dvd[7] = 1;
	while (dvd[7] & 1);

	return 0;
}

int dvd_readmem_array(u32 addr, void* buf, u32 size)
{
	u32* ptr = (u32*)buf;
	int rem = size;

	while (rem>0)
	{
		*ptr++ = dvd_readmem_32(addr);
		addr += 4;
		rem -= 4;
	}
	return 0;
}

int dvd_writemem_array(u32 addr, void* buf, u32 size)
{
	u32* ptr = (u32*)buf;
	int rem = size;

	while (rem > 0)
	{
		dvd_writemem_32(addr, *ptr++);
		addr += 4;
		rem -= 4;
	}
	return 0;
}

#define DEBUG_STOP_DRIVE 0
#define DEBUG_START_DRIVE 0x100
#define DEBUG_ACCEPT_COPY 0x4000
#define DEBUG_DISC_CHECK 0x8000

void dvd_motor_on_extra()
{
	dvd[0] = 0x2e;
	dvd[1] = 1;
	dvd[2] = (0xfe110000 | DEBUG_ACCEPT_COPY | DEBUG_START_DRIVE);
	dvd[3] = 0;
	dvd[4] = 0;
	dvd[5] = 0;
	dvd[6] = 0;
	dvd[7] = 1;
	while (dvd[7] & 1);
}

void* drive_patch_ptr(u64 driveVersion)
{
	if(driveVersion == 0x20020402)
		return &Drive04;
	if(driveVersion == 0x20010608)
		return &Drive06;
	if(driveVersion == 0x20010831)
		return &DriveQ;
	if(driveVersion == 0x20020823)
		return &Drive08;
	return 0;
}

void dvd_enable_patches() 
{
	// Get the drive date
	u8* driveDate = (u8*)memalign(32,32);
	memset(driveDate,0,32);
	drive_version(driveDate);
	
	u64 driveVersion = *(u64*)&driveDate[0];
	free(driveDate);
	
	if(!driveVersion) return;	// Unsupported drive

	void* patchCode = drive_patch_ptr(driveVersion);
	if(patchCode == NULL) {
		return;	// Unsupported drive
	}
	printf("Drive date %08X\r\nUnlocking drive\r\n",(u32)(driveVersion&0xFFFFFFFF));
	dvd_unlock();
	printf("Unlocking drive - done\r\nWrite patch\r\n");
	dvd_writemem_array(0xff40d000, patchCode, 0x1F0);
	dvd_writemem_32(0x804c, 0x00d04000);
	printf("Write patch - done\r\nSet extension %08X\r\n",dvd_get_error());
	dvd_setextension();
	printf("Set extension - done\r\nUnlock again %08X\r\n",dvd_get_error());
	dvd_unlock();
	printf("Unlock again - done\r\nDebug Motor On %08X\r\n",dvd_get_error());
	dvd_motor_on_extra();
	printf("Debug Motor On - done\r\nSet Status %08X\r\n",dvd_get_error());
	dvd_setstatus();
	printf("Set Status - done %08X\r\n",dvd_get_error());
	dvd_read_id();
	printf("Read ID %08X\r\n",dvd_get_error());
}


/* 
DVD_LowRead64(void* dst, unsigned int len, uint64_t offset)
  Read Raw, needs to be on sector boundaries (on a real DVD Drive)
  Has 8,796,093,020,160 byte limit (8 TeraBytes)
  Synchronous function.
    return -1 if offset is out of range
*/
int DVD_LowRead64(void* dst, u32 len, u32 offset)
{
	vu32* dvd = (vu32*)0xCC006000;
	
	if ((((u32)dst) & 0xC0000000) == 0x80000000) // cached?
		dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = 0xA8000000;
	dvd[3] = offset >> 2;
	dvd[4] = len;
	dvd[5] = (u32)dst;
	dvd[6] = len;
	dvd[7] = 3; // enable reading!
	while (dvd[7] & 1);

	DCInvalidateRange(dst, len);
	if (dvd[0] & 0x4)
		return 1;
	return 0;
}

/* 
DVD_Read(void* dst, uint64_t offset, int len)
  Reads from any offset and handles alignment from device
  Synchronous function.
    return -1 if offset is out of range
*/
s32 DVD_Read(void* dst, u32 offset, u32 len)
{
	u32 ol = len;
	s32 ret = 0;	
	u8 *sector_buffer = (u8*)memalign(32,2048);
	while (len)
	{
		uint32_t sector = offset / 2048;
		ret |= DVD_LowRead64(sector_buffer, 2048, sector * 2048);
		uint32_t off = offset & 2047;

		int rl = 2048 - off;
		if (rl > len)
			rl = len;
		memcpy(dst, sector_buffer + off, rl);	

		offset += rl;
		len -= rl;
		dst += rl;
	}
	free(sector_buffer);
	if(ret)
		return -1;

	return ol;
}

/* 
DVD_ReadID()
  Read the GameID (first 32 bytes) of a Game
    returns 0 on success
  Synchronous function.
*/
int DVD_ReadID(dvddiskid *diskID)
{
  volatile unsigned long* dvd = (volatile unsigned long*)0xCC006000;
	dvd[0] = 0x2E;
	dvd[1] = 0;
	dvd[2] = 0xA8000040;
	dvd[3] = 0;
	dvd[4] = 0x20;
	dvd[5] = (unsigned int)diskID;
	dvd[6] = 0x20;
	dvd[7] = 3; // enable reading!
	while (dvd[7] & 1);
	if (dvd[0] & 0x4)
		return 1;
	return 0;
}
