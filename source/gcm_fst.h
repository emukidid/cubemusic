#ifndef __GCM_FST_H
#define __GCM_FST_H

#define FST_ENTRY_SIZE 12

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

typedef struct {
	u32  discOffset;
	u32  fileSize;
	char storagePath[1024];
	const char *filename;
} AudioEntry;

void* get_fst();
void get_fst_details(char *FST, const char *searchFileName, u32 *file_offset, u32 *file_size);
#endif
