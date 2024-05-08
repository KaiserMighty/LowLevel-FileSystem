#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#include "fsPath.h"
#include "fsFreeSpace.h"
#include "mfs.h"

// Recursively delete everything in a directory and free the allocated space
int clearDir (directoryEntry* directory)
{
    for (int i = 2; i < directory[0].isDirectory; i++)
    {
        if (directory[i].isDirectory > 0)
        {
            directoryEntry* recurseDir = loadDir(&directory[i]);
            clearDir(recurseDir);
            free(recurseDir);
            recurseDir = NULL;
        }
        returnFreeSpace(directory[i].extents);
    }
    return 0;
}

// Return the first directoryDentry that has a null filename
int findUnusedEntry (directoryEntry* directory)
{
    for (int i = 0; i < directory[0].isDirectory; i++)
    {
        if (strcmp("\0", directory[i].filename) == 0)
        {
            return i;
        } 
    }
    return -1;
}

// Match the caller's string with filename in the directory
int findInDir (directoryEntry* directory, char* target)
{
    for (int i = 0; i < directory[0].isDirectory; i++)
    {
        if (strcmp(target, directory[i].filename) == 0)
        {
            return i;
        } 
    }
    return -1;
}

// Given a single directory entry, load the rest of the directory
directoryEntry* loadDir (directoryEntry* directory)
{
    directoryEntry* newDir = malloc(directory->isDirectory * sizeof(directoryEntry));
    int offset = 0;
    for (int i = 0; directory->extents[i].location != -1; i++)
    {
        LBAread(newDir+offset, directory->extents[i].count, directory->extents[i].location);
        offset += directory->extents[i].count * BLOCK_SIZE;
    }
    return newDir;
}

// "Middleware" between caller's string paths and system's structs
int parsePath (const char* pathname, pathInfo* info)
{
    // Safety error handling
    if (pathname == NULL) return -1;
    if (info == NULL) return -1;
    
    char* path = strdup(pathname);
    directoryEntry* startParent;
    if (path[0] == '/') // Absolute Path
    {
        startParent = getRoot();
    }
    else // Relative Path
    {
        startParent = getCwd();
    }
    directoryEntry* parent = startParent;
    char* savePtr = path;
    char* token = strtok_r (path, "/", &savePtr);
    if (token == NULL)
    {
        if (path[0] != '/') // Invalid Path
        {
            free(path);
            path = NULL;
            return -1;
        }
        else // Empty Path
        {
            info->index = -2;
            info->name = NULL;
            info->parent = parent;
            free(path);
            path = NULL;
            return 0;
        }
    }
    int index;
    char* token2;
    // Main Loop
    while (1)
    {
        index = findInDir(parent, token);
        token2 = strtok_r (NULL, "/", &savePtr);
        if (token2 == NULL) // Successfully found final path element
        {
            info->index = index;
            info->name = token;
            info->parent = parent;
            return 0;
        }
        if (index == -1) // Directory doesn't exist
        {
            free(path);
            path = NULL;
            return -1;
        }
        if (parent[index].isDirectory == 0) // Entry is not a directory
        {
            free(path);
            path = NULL;
            return -1;
        }
        // Move onto next path element
        directoryEntry* tempParent;
        if (parent[index].filesize == -1)
        {
            tempParent = getRoot();
        }
        else
        {
            tempParent = loadDir(&parent[index]);
        }
        if (parent != startParent)
        {
            free(parent->extents);
            free(parent);
        }
        parent = tempParent;
        token = token2;
    }
}