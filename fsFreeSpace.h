#ifndef FS_FREE_SPACE_H
#define FS_FREE_SPACE_H

typedef struct extent
{
    int location; // Beginning of Extent
    int count;    // Length of Extent
} extent;

int initFreeSpace (int totalBlocks);
int readFreeSpace (int blockNumber);
int exitFreeSpace ();
int useFreeSpace (extent* freeSpace);
int returnFreeSpace (extent* usedSpace);
extent* findFreeSpace (int numberOfBlocks, int minimumSize);
int writeExtents (void* buffer, extent* extents);
int copyExtents (extent* source, extent* destination);
int trimExtents (extent* extents, int lastBlock);
extent* appendExtents (extent* oldExtents, extent* newExtents);
extent* createSecondaryExtents (extent* extents, int lastIndex);
extent* createTertiaryExtents (extent* extents, int lastIndex);
int getBlock (extent* extents, int index);
int getLastBlock (extent* extents);

#endif