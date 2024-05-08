#ifndef FS_PATH_H
#define FS_PATH_H

#include "fsInit.h"

typedef struct pathInfo
{
	char* name;
	int index;
    directoryEntry* parent;
} pathInfo;

int clearDir (directoryEntry* directory);
int findUnusedEntry (directoryEntry* directory);
int findInDir (directoryEntry* directory, char* target);
directoryEntry* loadDir (directoryEntry* directory);
int parsePath (const char* pathname, pathInfo* info);

#endif