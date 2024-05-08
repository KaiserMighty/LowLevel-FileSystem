#ifndef FS_INIT_H
#define FS_INIT_H

#include "fsFreeSpace.h"
#include "fsLow.h"

#define BLOCK_SIZE 512 //number of bytes in a block (512 bytes)
#define SIGNATURE 0xDEADC0DE //our signature/magic number

typedef struct volumeControlblock{
	long signature; //signature
	int totalBlocks; //total blocks available in the volume
	int freeSpace; // location of free space extents
	int rootDirectory; //starting block for root directory
} volumeControlblock;

typedef struct directoryEntry{
	char filename[255]; //file name
	time_t createdDatetime; //file creation timestamp
	time_t modifiedDatetime; //last modified timestamp
	time_t accessedDatetime; // Last Opened Timestamp
	extent extents[28];
	int filesize;
	int isDirectory; // Directory Flag and Entry Count
} directoryEntry;

directoryEntry* getRoot();
directoryEntry* createDirectory(int NumEntries, directoryEntry* parent);
int initFileSystem (uint64_t numberOfBlocks, uint64_t blockSize);


#endif