#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#include "fsLow.h"

#define BLOCK_SIZE 512 //number of bytes in a block (512 bytes)
#define EXTENT_COUNT BLOCK_SIZE/8 // Enough array elements to fill the block
#define LOCATION 1 // Location of Free Space Extents

typedef struct extent
{
	int location; // Beginning of Extent
	int count; // Length of Extent
} extent;

extent *freeSpaceExtents;

// Initialize free space block and store in memory
int initFreeSpace (int totalBlocks)
{
    freeSpaceExtents = malloc(EXTENT_COUNT * sizeof(extent));
    if (freeSpaceExtents == NULL)
    {
        return -1;
    }
    freeSpaceExtents[0].location = LOCATION+1;
    freeSpaceExtents[0].count = totalBlocks-LOCATION+1;
    for (int i = 1; i < EXTENT_COUNT; i++)
    {
        freeSpaceExtents[i].location = -1;
        freeSpaceExtents[i].count = -1;
    }
    LBAwrite(freeSpaceExtents, 1, LOCATION);
    return LOCATION;
}

// Read already initialized free space block and store in memory
int readFreeSpace (int blockNumber)
{
    freeSpaceExtents = malloc(EXTENT_COUNT * sizeof(extent));
    if (freeSpaceExtents == NULL)
    {
        return -1;
    }
    LBAread(freeSpaceExtents, 1, blockNumber);
    return 0;
}

// Cleanup function for exiting file system
int exitFreeSpace ()
{
    free(freeSpaceExtents);
    freeSpaceExtents = NULL;
    return 0;
}

int useFreeSpace (extent* freeSpace)
{
    // Iterate through all argument elements
    for (int i = 0; freeSpace[i].location != -1; i++)
    {
        // Iterate through all free space extents
        for (int j = 0; j < EXTENT_COUNT; j++)
        {
            // Find matching extents
            if (freeSpaceExtents[j].location == freeSpace[i].location)
            {
                // If using the entire extent, just mark the whole thing as used
                if (freeSpaceExtents[j].count == freeSpace[i].count)
                {
                    freeSpaceExtents[j].location = -1;
                }
                // If only using partial, offset the modify location and count as needed 
                else
                {
                    freeSpaceExtents[j].location += freeSpace[i].count;
                    freeSpaceExtents[j].count -= freeSpace[i].count;
                }
                break;
            }
        }
    }
    LBAwrite(freeSpaceExtents, 1, LOCATION);
    return 0;
}

int returnFreeSpace (extent* usedSpace)
{
    int returnedFlag;
    for (int i = 0; usedSpace[i].location != -1; i++)
    {
        returnedFlag = 0;
        // Iterate through all free space extents
        for (int j = 0; j < EXTENT_COUNT; j++)
        {
            // Find matching extents
            if (freeSpaceExtents[j].location == usedSpace[i].location + usedSpace[i].count)
            {
                freeSpaceExtents[j].location -= usedSpace[i].count;
                freeSpaceExtents[j].count += usedSpace[i].count;
                returnedFlag = 1;
                break;
            }
        }
        if (returnedFlag != 1)
        {
            // Return to next free slot
            for (int j = 0; j < EXTENT_COUNT; j++)
            {
                if (freeSpaceExtents[j].location == -1)
                {
                    freeSpaceExtents[j].location = usedSpace[i].count;
                    freeSpaceExtents[j].count = usedSpace[i].count;
                    returnedFlag = 1;
                    break;
                }
                // if returnedFlag is still not 1, create secondary/tertiary extents
            }
        }
    }
    LBAwrite(freeSpaceExtents, 1, LOCATION);
    return 0;
}

extent* findFreeSpace (int numberOfBlocks, int minimumSize)
{
    extent *freeBlocks = malloc((numberOfBlocks + 1) * sizeof(extent));
    if (freeBlocks == NULL)
    {
        return NULL; // Memory allocation failed
    }
    int extentCount = 0;
    int blocksFound = 0;
    // Iterate through the extents array
    for (int i = 0; i < EXTENT_COUNT; i++)
    {
        if (freeSpaceExtents[i].location == -1)
        {
            // Extent not free
            continue;
        }

        // Contiguous free blocks available
        if (freeSpaceExtents[i].count >= numberOfBlocks)
        {
            // Return the block number of the current extent
            freeBlocks[0].location = freeSpaceExtents[i].location;
            freeBlocks[0].count = numberOfBlocks;
            freeBlocks[1].location = -1; // Sentinel value
            useFreeSpace(freeBlocks);
            return freeBlocks;
        }
        // No contiguous free blocks available
        else
        {
            freeBlocks[extentCount].location = freeSpaceExtents[i].location;
            // Found enough discontiguous blocks
            if (freeSpaceExtents[i].count >= numberOfBlocks-blocksFound)
            {
                freeBlocks[extentCount++].count = numberOfBlocks;
                freeBlocks[extentCount].location = -1; // Sentinel value
                useFreeSpace(freeBlocks);
                return freeBlocks;
            }
            // Still need more discontiguous blocks
            else
            {
                freeBlocks[extentCount++].count = freeSpaceExtents[i].count;
                blocksFound += freeSpaceExtents[i].count;
            }
        }
    }
    // If we reach this point, not enough free space is available
    freeBlocks[0].location = -1; // Sentinel value
    return freeBlocks;
}

int writeExtents (void* buffer, extent* extents)
{
    int blocks = 0;
	for (int i = 0; extents[i].location != -1; i++)
    {
        LBAwrite(buffer+(blocks*BLOCK_SIZE), extents[i].count, extents[i].location);
        blocks += extents[i].count;
	}
    return 0;
}

int copyExtents (extent* source, extent* destination)
{
    for (int i = 0; source[i].location != -1; i++)
    {
		destination[i].location = source[i].location;
		destination[i].count = source[i].count;
		destination[i+1].location = -1;
	}
    return 0;
}

int trimExtents (extent* extents, int lastBlock)
{
    // Locate last used block
    for (int i = 0; extents[i].location != -1; i++)
    {
        if (lastBlock <= extents[i].count)
        {
            extent* returnExtents = malloc((EXTENT_COUNT) * sizeof(extent));
            int k = 1; // Returning extents index
            // Begin returning extents after last used block
            returnExtents[0].location = extents[i].location + lastBlock;
            returnExtents[0].count = extents[i].count - lastBlock;
            returnExtents[1].location = -1;
            // End used extents after last used block
            extents[i].location += lastBlock;
            extents[i].count -= lastBlock;
            // Copy used extents after last used block to returning extents;
            for (int j = i+1; extents[j].location != -1; j++)
            {
                returnExtents[k].location = extents[j].location;
                returnExtents[k].count = extents[j].count;
                returnExtents[++k].location = -1;
            }
            extents[i+1].location = -1;
            returnFreeSpace(returnExtents);
            return 0; // Space returned to free
        }
        else
        {
            lastBlock -= extents[i].count;
        }
    }
    return -1; // Space is still used
}

extent* appendExtents (extent* oldExtents, extent* newExtents)
{
    // FIGURE OUT A WAY TO DISTINGUISH BETWEEN FILE (28 EXTENTS) AND NOT (64 EXTENTS)
    int extentsValue[EXTENT_COUNT][2];
    int extentsCount = 0;
    // Fill from old extents
    for (int i = 0; oldExtents[i].location != -1; i++)
    {
        extentsValue[extentsCount][0] = oldExtents[i].location;
        extentsValue[extentsCount][1] = oldExtents[i].count;
        extentsCount++;
    }
    // Fill from new extents
    for (int i = 0; newExtents[i].location != -1; i++)
    {
        extentsValue[extentsCount][0] = newExtents[i].location;
        extentsValue[extentsCount][1] = newExtents[i].count;
        extentsCount++;
    }
    // Populate merged extents
    extent *mergedExtents = malloc((extentsCount + 1) * sizeof(extent));
    for (int i = 0; extentsCount > i; i++)
    {
        mergedExtents[i].location = extentsValue[i][0];
        mergedExtents[i].count = extentsValue[i][1];
    }
    // "Null Terminator"
    mergedExtents[extentsCount].location = -1;
    mergedExtents[extentsCount].count = -1;
    return mergedExtents;
}

extent* createSecondaryExtents (extent* extents, int lastIndex)
{
    extent* freeExtent = findFreeSpace(1,1);
    if (freeExtent[0].location != -1)
    {
        extent* secondaryExtents;
        // Move last extent to first secondary extent
        secondaryExtents[0].location = extents[lastIndex].location;
        secondaryExtents[0].count = extents[lastIndex].count;
        // Change last extent to location of secondary extent   
        extents[lastIndex].location = freeExtent[0].location;
        extents[lastIndex].count = -1;
        // Set remaining secondary extents as unused
        for (int i = 1; i < EXTENT_COUNT; i++)
        {
            secondaryExtents[i].location = -1;
            secondaryExtents[i].count = -1;
        }

        LBAwrite(secondaryExtents, 1, freeExtent[0].location);
        free(freeExtent);
        freeExtent = NULL;
        return secondaryExtents;
    }
    return freeExtent; // No free blocks available
}

int* createTertiaryExtents (extent* extents, int lastIndex)
{
    extent* freeExtent = findFreeSpace(1,1);
    if (freeExtent[0].location != -1)
    {
        int* tertiaryExtents = malloc(EXTENT_COUNT * sizeof(int));
        if (tertiaryExtents == NULL)
        {
            return NULL;
        }
        // Move secondary extent to first tertiary extent
        tertiaryExtents[0] = extents[lastIndex].location;
        // Update original extents with location of tertiary extents
        extents[lastIndex].location = freeExtent[0].location;
        extents[lastIndex].count = -2;
        // Set remaining tertiary extents as unused
        for (int i = 1; i < EXTENT_COUNT; i++)
        {
            tertiaryExtents[i] = -1;
        }

        LBAwrite(tertiaryExtents, 1, freeExtent[0].location);
        free(freeExtent);
        freeExtent = NULL;
        return tertiaryExtents;
    }
    return NULL; // No free blocks available
}

int getBlock (extent* extents, int index)
{
    for (int i = 0; extents[i].location != -1; i++)
    {
        // Index is within current extent
        if (index <= extents[i].count)
        {
            return extents[i].location + index - 1;
        }
        // Index is not within current extent
        else
        {
            index -= extents[i].count;
        }
    }
    return -1; // Block not found
}

int getLastBlock (extent* extents)
{
    // Simply sum all the counts
    int lastIndex = 0;
    for (int i = 0; extents[i].location != -1; i++)
    {
        lastIndex += extents[i].count;
    }
    return lastIndex;
}