#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>			// for malloc
#include <string.h>			// for memcpy
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include "b_io.h"
#include "fsPath.h"
#include "fsInit.h"
#include "fsFreeSpace.h"
#include "mfs.h"

#define MAXFCBS 20
#define B_CHUNK_SIZE 512
// For "lastaction"
#define OPEN 0
#define READ 1
#define WRITE 2
#define SEEK 3

typedef struct b_fcb
	{
	directoryEntry* fi; //holds information about file
	directoryEntry* parent; //holds information about parent
	char * buff;		//holds the open file buffer
	int index;		//holds the current position in the buffer
    int blockIndex; //file position, but converted to block
    int lastBlock; //the last block that was written to, for trimming
    int trim; //flag to know if we need to trim extents or not
	int flags; //what flags the file was opened with
	int filePosition; //holds the current position in the file
	int accessmode; //holds the current position in the file
	int lastaction; //0 open, 1 read, 2 write, 3 seek
	} b_fcb;
	
b_fcb fcbArray[MAXFCBS];

int startup = 0;	//Indicates that this has not been initialized
int rwMask = O_WRONLY | O_RDONLY | O_RDWR; //bitmask to check for O_WRONLY, O_RDONLY, O_RDWR flags

//Method to initialize our file system
void b_init ()
	{
	//init fcbArray to all free
	for (int i = 0; i < MAXFCBS; i++)
		{
		fcbArray[i].buff = NULL; //indicates a free fcbArray
		}
		
	startup = 1;
	}

//Method to get a free FCB element
b_io_fd b_getFCB ()
	{
	for (int i = 0; i < MAXFCBS; i++)
		{
		if (fcbArray[i].buff == NULL)
			{
			return i;		//Not thread safe (But do not worry about it for this assignment)
			}
		}
	return (-1);  //all in use
	}
	
// Interface to open a buffered file
// Modification of interface for this assignment, flags match the Linux flags for open
// O_RDONLY, O_WRONLY, or O_RDWR
b_io_fd b_open (char * filename, int flags)
	{
	b_io_fd returnFd;

	if (startup == 0) b_init();  //Initialize our system

	pathInfo pathInfoptr;
    int result = parsePath(filename, &pathInfoptr);
    if(result == -1) {
        printf("Invalid Path\n");
        return -1;
    }
    //Path is not a file
    if(pathInfoptr.index != -1 && pathInfoptr.parent[pathInfoptr.index].isDirectory != 0) {
        printf("Not a File\n");
        return -1;
    }
    //File already exists and O_CREAT flag is set
    if(pathInfoptr.index != -1 && ((flags & O_CREAT) == 1 && (flags & O_TRUNC) == 0)) {
        printf("File Already Exists\n");
        return -1;
    }
    //Could not find file and O_CREAT flag is not set
	if(pathInfoptr.index == -1 && (flags & O_CREAT) == 0){
        printf("File Does Not Exist\n");
        return -1;
    }
	
	returnFd = b_getFCB();				// get our own file descriptor
										// check for error - all used FCB's
	if(returnFd == -1){
		//FCB's are all used
		return -1;
	}

	fcbArray[returnFd].buff = malloc(B_CHUNK_SIZE); //malloc buffer
	fcbArray[returnFd].flags = flags; //set flags for file
	fcbArray[returnFd].index = 0; //set current position in buffer
	fcbArray[returnFd].parent = pathInfoptr.parent; //set parent
	fcbArray[returnFd].accessmode = flags & rwMask; // read only, write only, read/write
	fcbArray[returnFd].lastaction = OPEN; //last action was open

	if(pathInfoptr.index == -1 && (flags & O_CREAT)){
		//Could not find file and O_CREAT flag is set
		//create empty file
		pathInfoptr.index = findUnusedEntry(pathInfoptr.parent);
		fcbArray[returnFd].fi = &pathInfoptr.parent[pathInfoptr.index]; //set file pointer
    	strcpy(fcbArray[returnFd].fi->filename, pathInfoptr.name);
    	fcbArray[returnFd].fi->isDirectory = 0;
		extent* freeBlocks = findFreeSpace(50, 1);
		copyExtents(freeBlocks, fcbArray[returnFd].fi->extents);
		fcbArray[returnFd].fi->filesize = 0; //set fileSize of file to 0 to make it empty
		fcbArray[returnFd].filePosition = 0; //set initial bytes read
		fcbArray[returnFd].blockIndex = 2; //set location of first block of file
		fcbArray[returnFd].fi->createdDatetime = time(0);
		fcbArray[returnFd].fi->modifiedDatetime = time(0);
	}
	else if(pathInfoptr.index != -1 && (flags & O_APPEND)){
		//File exists and O_APPEND flag is set
		//set location to end of file
		fcbArray[returnFd].fi = &pathInfoptr.parent[pathInfoptr.index]; //set file pointer
		fcbArray[returnFd].filePosition = fcbArray[returnFd].fi->filesize; //set bytes read to file size
		fcbArray[returnFd].blockIndex = getLastBlock(fcbArray[returnFd].fi->extents);
	}
	else if(pathInfoptr.index != -1 && (flags & O_TRUNC)){
		//File exists and O_TRUNC flag is set
		//make file empty
		fcbArray[returnFd].fi = &pathInfoptr.parent[pathInfoptr.index]; //set file pointer
		fcbArray[returnFd].fi->filesize = 0; //set fileSize of file to 0 to make it empty
		fcbArray[returnFd].filePosition = 0; //set initial bytes read
		fcbArray[returnFd].blockIndex = 2; //set location of first block of file
		fcbArray[returnFd].trim = 1;
	}
	else if(pathInfoptr.index != -1){
		//File exists and O_CREAT, O_APPEND, O_TRUNC flags are not set
		fcbArray[returnFd].fi = &pathInfoptr.parent[pathInfoptr.index]; //set file pointer
		fcbArray[returnFd].filePosition = 0; //set initial bytes read
		fcbArray[returnFd].blockIndex = 2; //set location of first block of file
	}
	fcbArray[returnFd].fi->accessedDatetime = time(0);
	fcbArray[returnFd].lastBlock = fcbArray[returnFd].blockIndex;
    writeExtents(pathInfoptr.parent, pathInfoptr.parent[0].extents);
	return (returnFd);						// all set
	}


// Interface to seek function	
int b_seek (b_io_fd fd, off_t offset, int whence)
	{
	if (startup == 0) b_init();  //Initialize our system

	// check that fd is between 0 and (MAXFCBS-1)
	if ((fd < 0) || (fd >= MAXFCBS))
		{
		return (-1); 					//invalid file descriptor
		}
	// if write was last, write buffer to file
	if (fcbArray[fd].lastaction == WRITE && fcbArray[fd].index != 0) {
		int blockNumber = getBlock(fcbArray[fd].fi->extents, fcbArray[fd].blockIndex++);
		LBAwrite(fcbArray[fd].buff, 1, blockNumber);
	}
	fcbArray[fd].lastaction = SEEK;
	if (offset <= fcbArray[fd].fi->filesize) {
		if(whence == SEEK_SET) {
			fcbArray[fd].filePosition = offset; //set the file offset to offset
		} else if (whence == SEEK_CUR) {
			fcbArray[fd].filePosition += offset; //set the file offset to current offset + 
		} else if (whence == SEEK_END) {
			fcbArray[fd].filePosition = fcbArray[fd].fi->filesize + (offset);
		}
	}
	fcbArray[fd].index = 0; //clear out buffer
	return fcbArray[fd].filePosition;
	}



// Interface to write function	
int b_write (b_io_fd fd, char * buffer, int count)
	{
	if (startup == 0) b_init();  //Initialize our system

	// check that fd is between 0 and (MAXFCBS-1)
	if ((fd < 0) || (fd >= MAXFCBS))
		{
		return (-1); 					//invalid file descriptor
		}
		
	// check if we have permissions
	if (fcbArray[fd].accessmode == O_RDONLY)
	{
		return 0;
	}

	// If writing after seeking, set block index and file size
	if (fcbArray[fd].lastaction == SEEK)
	{
		if (fcbArray[fd].filePosition > fcbArray[fd].fi->filesize)
		{
			fcbArray[fd].fi->filesize = fcbArray[fd].filePosition;
		}
		fcbArray[fd].blockIndex = (fcbArray[fd].filePosition+B_CHUNK_SIZE-1)/B_CHUNK_SIZE;
	}
	int bytesWritten = 0;
	fcbArray[fd].lastaction = WRITE;
	// If write request goes beyond extents, append more blocks
	int sizeInBlocks = getLastBlock(fcbArray[fd].fi->extents);
	if (sizeInBlocks < fcbArray[fd].blockIndex)
	{
		extent* extentsToAppend = findFreeSpace(sizeInBlocks*2, 1);
		extent* appendedExtents = appendExtents(fcbArray[fd].fi->extents, extentsToAppend);
		copyExtents(appendedExtents, fcbArray[fd].fi->extents);
		free(extentsToAppend);
		free(appendedExtents);
		extentsToAppend = NULL;
		appendedExtents = NULL;
		fcbArray[fd].trim = 1;
	}
	// Loop until write request is fulfilled
	while (count > bytesWritten)
	{
		// Write request is greater than chunk size, copy directly into file
		if (count - bytesWritten >= B_CHUNK_SIZE && fcbArray[fd].index == 0)
		{
			int blockNumber = getBlock(fcbArray[fd].fi->extents, fcbArray[fd].blockIndex++);
			LBAwrite(buffer + bytesWritten, 1, blockNumber);
			bytesWritten += B_CHUNK_SIZE;
			continue;
		}
		// Write request is smaller than chunk size, copy into buffer
		if (count - bytesWritten < B_CHUNK_SIZE)
		{
			// Temp variables for readability
			int index = fcbArray[fd].index;
			int bufferSize = B_CHUNK_SIZE - fcbArray[fd].index;
			// More bytes to write than can be supported in the buffer
			if (count - bytesWritten > bufferSize)
			{
				memcpy(fcbArray[fd].buff + index, buffer + bytesWritten, bufferSize);
				bytesWritten += bufferSize;
				int blockNumber = getBlock(fcbArray[fd].fi->extents, fcbArray[fd].blockIndex++);
				LBAwrite(fcbArray[fd].buff, 1, blockNumber);
				fcbArray[fd].index = 0;
			}
			// All bytes can fit inside buffer
			else
			{
				memcpy(fcbArray[fd].buff + index, buffer + bytesWritten, count);
				bytesWritten += count;
				fcbArray[fd].index += count;
				// If buffer was fully written, write to file and reset it
				if (fcbArray[fd].index == B_CHUNK_SIZE)
				{
					int blockNumber = getBlock(fcbArray[fd].fi->extents, fcbArray[fd].blockIndex++);
					LBAwrite(fcbArray[fd].buff, 1, blockNumber);
					fcbArray[fd].index = 0;
				}
			}
		}
	}
	// Set "lastBlock" if we just wrote to a block larger
	if (fcbArray[fd].lastBlock < fcbArray[fd].blockIndex)
	{
		fcbArray[fd].lastBlock = fcbArray[fd].blockIndex;
	}
	fcbArray[fd].fi->filesize += bytesWritten;
	fcbArray[fd].fi->modifiedDatetime = time(0);
    return bytesWritten;
	}

int b_read (b_io_fd fd, char * buffer, int count)
	{

	if (startup == 0) b_init();  //Initialize our system

	// check that fd is between 0 and (MAXFCBS-1)
	if ((fd < 0) || (fd >= MAXFCBS))
		{
		return (-1); 					//invalid file descriptor
		}

	// check if we have permissions
	if (fcbArray[fd].accessmode == O_WRONLY)
	{
		return 0;
	}

	// Check if we are already at EOF
	if (fcbArray[fd].fi->filesize - fcbArray[fd].filePosition <= 0)
	{
		return 0;
	}
	// If reading after seeking, set block index
	if (fcbArray[fd].lastaction == SEEK)
	{
		fcbArray[fd].blockIndex = (fcbArray[fd].filePosition+B_CHUNK_SIZE-1)/B_CHUNK_SIZE;
	}
	int bytesRead = 0;
	fcbArray[fd].lastaction = READ;
	// If read request goes beyond EOF, set request to end at EOF
	if (count > fcbArray[fd].fi->filesize - fcbArray[fd].filePosition)
	{
		count = fcbArray[fd].fi->filesize - fcbArray[fd].filePosition;
	}
	// Loop until read request is fulfilled
	while (count > bytesRead)
	{
		// Buffer is not empty, empty it before LBA read
		if (fcbArray[fd].index != 0)
		{
			// Temp variables for readability
			int index = fcbArray[fd].index;
			int bufferSize = B_CHUNK_SIZE - fcbArray[fd].index;
			// Buffer does not have enough bytes for read request
			if (count > bufferSize)
			{
				memcpy(buffer + bytesRead, fcbArray[fd].buff + index, bufferSize);
				bytesRead += bufferSize;
				fcbArray[fd].index = 0;
			}
			// Buffer has enough bytes for read request
			else
			{
				memcpy(buffer + bytesRead, fcbArray[fd].buff + index, count);
				bytesRead += count;
				fcbArray[fd].index += count;
				// If buffer was fully read, reset it
				if (fcbArray[fd].index == B_CHUNK_SIZE)
				{
					fcbArray[fd].index = 0;
				}
				break;
			}
		}
		// Read request is greater than chunk size, copy directly to user buffer
		if (count - bytesRead > B_CHUNK_SIZE)
		{
			int blockNumber = getBlock(fcbArray[fd].fi->extents, fcbArray[fd].blockIndex++);
			LBAread(buffer + bytesRead, 1, blockNumber);
			bytesRead += B_CHUNK_SIZE;
		}
		// Read request is smaller than chunk size, copy to our buffer
		else
		{
			int blockNumber = getBlock(fcbArray[fd].fi->extents, fcbArray[fd].blockIndex++);
			LBAread(fcbArray[fd].buff, 1, blockNumber);
			memcpy(buffer + bytesRead, fcbArray[fd].buff, count - bytesRead);
			fcbArray[fd].index = count - bytesRead;
			bytesRead += count - bytesRead;
		}
	}
	fcbArray[fd].filePosition += bytesRead;
    return bytesRead;
	}
	
// Interface to Close the file	
int b_close (b_io_fd fd)
	{
	// Empty buffer if it still has something in it
	if (fcbArray[fd].index != 0 && fcbArray[fd].lastaction == WRITE)
	{
		int blockNumber = getBlock(fcbArray[fd].fi->extents, fcbArray[fd].blockIndex++);
		LBAwrite(fcbArray[fd].buff, 1, blockNumber);
	}
	// Return excess blocks to free space
	if (fcbArray[fd].trim == 1)
	{
		trimExtents(fcbArray[fd].fi->extents, fcbArray[fd].lastBlock);
	}
	// Write to disk and cleanup
	writeExtents(fcbArray[fd].parent, fcbArray[fd].parent[0].extents);
	freeDir(fcbArray[fd].parent);
	fcbArray[fd].fi = NULL;
	free(fcbArray[fd].buff);
	fcbArray[fd].buff = NULL;
	return 0;
	}
