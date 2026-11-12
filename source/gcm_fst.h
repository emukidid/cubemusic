#ifndef __GCM_FST_H
#define __GCM_FST_H

#include <gccore.h>

/* 	The Retail Game DVD Disk Header
	Info source: YAGCD
	Size of Struct: 0x2440	*/
typedef struct {
	u8  ConsoleID;		//G = Gamecube, R = Wii.
	u8  GamecodeA;		//2 Ascii letters to indicate the GameID.
	u8  GamecodeB;		//2 Ascii letters to indicate the GameID.
	u8	CountryCode;	//J=JAP . P=PAL . E=USA . D=OoT MasterQuest
	u8	MakerCodeA;		//Eg 08 = Sega etc.
	u8  MakerCodeB;
	u8	DiscID;
	u8	Version;
	u8	AudioStreaming; //01 = Enable it. 00 = Don't
	u8 	StreamBufSize;	//For the AudioEnable. (always 00?)
	u8	unused_1[18];
	u32 DVDMagicWord;	//0xC2339F3D
	char GameName[64];	//String Name of Game, rarely > 32 chars
	u8  unused_2[416];
	u32 NKitMagicWord;
	u32 NKitVersion;
	u32 ImageCRC;
	u32 ForceCRC;
	u32 ImageSize;
	u32 JunkID;
	u8  unused_3[488];
	u32 ApploaderSize;
	u32 ApploaderFunc1;
	u32 ApploaderFunc2;
	u32 ApploaderFunc3;
	u8  unused_4[16];
	u32 DOLOffset;		//offset of main executable DOL (bootfile)
	u32 FSTOffset;		//offset of the FST ("fst.bin")
	u32	FSTSize;		//size of FST
	u32	MaxFSTSize;		//maximum size of FST (usually same as FSTSize)*
	u32 FSTAddress;
	u32 UserPos;		//user position(?)
	u32 UserLength;		//user length(?)
	u32 unused_5;
	u32 DebugMonSize;
	u32 SimMemSize;
	u32 ArgOffset;
	u32 DebugFlag;
	u32 TRKLocation;
	u32 TRKSize;
	u32 RegionCode;
	u32 TotalDisc;
	u32 LongFileName;
	u32 PADSpec;
	u32 DOLLimit;
	u8  unused_6[8148];
	// *Multiple DVDs must use it, to properly reside all FSTs.
} DiskHeader __attribute__((aligned(32)));

/* One file on the disc, by its full path from the root ("Music/Chase/Chase01.asf"). */
typedef struct {
	const char *path;
	u32 offset;
	u32 size;
} DiscFile;

/* Reads the disc header and file table and indexes every file by full path.
 * Replaces any previous index. Returns the number of files, or -1 on a read error.
 * game_name (64 bytes, may be NULL) receives the header's game name. */
int fst_load(char *game_name);

/* Every file on the disc, by index (0..fst_count()-1), sorted by path. */
int fst_count(void);
const DiscFile *fst_file(int index);

/* Frees the index. */
void fst_free(void);

/* Finds a file by full path (case-insensitive, '/' or '\\' separators, a
 * leading separator is ignored). If no file has that exact path, the path is
 * matched as the end of a disc path on a folder boundary ("rui_samusL.dsp"
 * finds "Audio/rui_samusL.dsp"); with several such matches the one nearest
 * the root wins. NULL if nothing matches. */
const DiscFile *fst_find(const char *path);

#endif
