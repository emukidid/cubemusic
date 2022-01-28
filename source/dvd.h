#ifndef __DVD_H
#define __DVD_H

#include <gccore.h>

#define CHUNK_SIZE    256*1024
#define DISC_SIZE   0x57058000
#define WII_D5_SIZE 0x118240000LL
#define WII_D9_SIZE 0x230480000LL

unsigned int dvd_get_error(void);
void dvd_set_streaming(char stream);
void dvd_motor_off();
void dvd_motor_on_extra();
int DVD_ReadID(dvddiskid *diskID);
s32 DVD_Read(void* dst, u32 offset, u32 len);
void drive_version(u8 *buf);
void dvd_reset();
#endif
