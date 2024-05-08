#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#include "fsInit.h"
#include "fsPath.h"
#include "mfs.h"

directoryEntry* rootDir;

directoryEntry* getRoot() {
	return rootDir;
}

directoryEntry* createDirectory(int NumEntries, directoryEntry* parent) {
	int bytesNeeded; // determine the the number of bytes needed
	int blocksNeeded; //determine how many blocks are required
	int totalBytes; //determines the total amount of bytes to malloc
	int actnumEntries; //the real number of directory entries 
	directoryEntry* myDirectory; //pointer to the Directory Entry Structure
	extent* freeSpace;

	bytesNeeded = NumEntries * sizeof(directoryEntry);
	blocksNeeded = (bytesNeeded + (BLOCK_SIZE - 1)) / BLOCK_SIZE;
	totalBytes = blocksNeeded * BLOCK_SIZE;
	actnumEntries = totalBytes / sizeof(directoryEntry);

	myDirectory = malloc(totalBytes);
	//call to get free space blocks here
	freeSpace = findFreeSpace(blocksNeeded, 1);

	//initialize each directory entry
	for(int i = 2; i < actnumEntries; i++) {
		myDirectory[i].filename[0] = '\0';
		myDirectory[i].filesize = 0;
		myDirectory[i].isDirectory = 0;
		myDirectory[i].extents[0].location = -1;
		myDirectory[i].extents[0].count = -1;
	}

	//seting up the . and .. directories below.

	strcpy(myDirectory[0].filename, ".");
	myDirectory[0].filesize = actnumEntries * sizeof(directoryEntry);
	copyExtents(freeSpace, myDirectory[0].extents);
	myDirectory[0].isDirectory = NumEntries;
	myDirectory[0].createdDatetime = time(NULL);


	strcpy(myDirectory[1].filename, "..");
	if (parent == NULL) { //we are in root directory
		myDirectory[1].isDirectory = myDirectory[0].isDirectory;
		copyExtents(myDirectory[0].extents, myDirectory[1].extents);
		myDirectory[1].filesize = -1; //root sentinal
		myDirectory[0].filesize = -1; //root sentinal
	} else { //not root directory
		myDirectory[1].isDirectory = parent[0].isDirectory;
		myDirectory[1].filesize = parent[0].filesize;
		myDirectory[1].createdDatetime = myDirectory[0].createdDatetime;
		copyExtents(parent[0].extents, myDirectory[1].extents);
	}

	//write directory to disk
	writeExtents(myDirectory, freeSpace);
	return myDirectory;

}
	
	int initFileSystem (uint64_t numberOfBlocks, uint64_t blockSize)
	{
	printf ("Initializing File System with %ld blocks with a block size of %ld\n", numberOfBlocks, blockSize);

	volumeControlblock* vcbPointer; //pointer to vcb struct
	vcbPointer = malloc(BLOCK_SIZE); //mallocing memory for ptr
	LBAread(vcbPointer, 1, 0); //reading the 0th block of volume and casting vcb ptr over it

	//this is our drive, just read it
	if(vcbPointer->signature == SIGNATURE){
		readFreeSpace(vcbPointer->freeSpace);
		directoryEntry* tempRoot = malloc(sizeof(directoryEntry));
		LBAread(tempRoot, 1, vcbPointer->rootDirectory);
		rootDir = loadDir(tempRoot);
		free(tempRoot);
		tempRoot = NULL;
		setCwd(rootDir);
		char* rootString = malloc(sizeof(char)*2);
		strcpy(rootString, "/");
		setCwdString(rootString);
	}
	//this is not our drive, "format" it
	else{
		vcbPointer->signature = SIGNATURE;
		vcbPointer->freeSpace = initFreeSpace(numberOfBlocks);
		vcbPointer->totalBlocks = numberOfBlocks;
		rootDir = createDirectory(50, NULL);
		vcbPointer->rootDirectory = 2;
		LBAwrite(vcbPointer, 1, 0);
		setCwd(rootDir);
		char* rootString = malloc(sizeof(char)*2);
		strcpy(rootString, "/");
		setCwdString(rootString);
	}

	free(vcbPointer);
	vcbPointer = NULL;
	return 0;
	}
	
void exitFileSystem ()
	{
	//cleanup memory
	directoryEntry* CWD = getCwd();
	if (CWD != rootDir)
	{
		free(CWD);
		CWD = NULL;
	}
	free(rootDir);
	rootDir = NULL;
	exitFreeSpace();
	return;
	}