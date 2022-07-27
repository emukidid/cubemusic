#include <stdio.h>
#include <gccore.h>		/*** Wrapper to include common libogc headers ***/
#include <ogcsys.h>		/*** Needed for console support ***/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include "gcm_fst.h"
#include "dvd.h"

#define FST_ENTRY_SIZE 12

// Parse disc header & read FST into a buffer
void* get_fst() {
	DiskHeader header;
	char	*FST;
	
	// Grab disc header
	memset(&header,0,sizeof(DiskHeader));
	if(DVD_Read(&header, 0, sizeof(DiskHeader)) != sizeof(DiskHeader)) {
		printf("Header size didn't read\r\n");
		return NULL;
	}
	
	// Alloc and read FST
	FST=(char*)memalign(32,header.FSTSize); 
	printf("FST size is %i\r\n", header.FSTSize);
	if(!FST) return NULL;

	if(DVD_Read(FST, header.FSTOffset, header.FSTSize) != header.FSTSize) {
		printf("FST size didn't match, need %i\r\n", header.FSTSize);
		free(FST);
		return NULL;
	}
	return FST;
}

// Populate the file_offset and file_size for searchFileName from the GCM FST
void get_fst_details(char *FST, const char *searchFileName, u32 *file_offset, u32 *file_size) {
	u32 filename_offset, entries, string_table_offset, offset, i;
	char filename[256];
	// number of entries and string table location
	entries = *(unsigned int*)&FST[8];
	string_table_offset=12*entries; 
    
	// go through every entry
	for (i=1;i<entries;i++) 
	{ 
		offset=i*0x0c; 
		if(FST[offset]==0) //skip directories
		{ 
			filename_offset=(unsigned int)FST[offset+1]*256*256+(unsigned int)FST[offset+2]*256+(unsigned int)FST[offset+3]; 
			memset(filename,0,256);
			strcpy(filename,&FST[string_table_offset+filename_offset]); 
			char *relativename = strrchr(searchFileName, '/');
			if(!strcasecmp(filename,relativename ? relativename+1 : searchFileName)) 
			{
				memcpy(file_offset,&FST[offset+4],4);
				memcpy(file_size,&FST[offset+8],4);
				return;
			}		
		} 
	}
	*file_offset = 0;
	*file_size = 0;
}

