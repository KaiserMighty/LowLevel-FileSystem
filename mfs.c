#include "mfs.h"
#include "fsPath.h"
#include "string.h"
#include "fsInit.h"
#include "fsFreeSpace.h"
#include <stdlib.h>
#include <stdio.h>

directoryEntry* cwd; // loaded current working directory
char* currentworkingDirectory; // the string containing the current working directory path
#define MAXPATH 100

directoryEntry* getCwd() {
	return cwd;
}

int setCwd(directoryEntry* directory) {
	cwd = directory;
    return 0;
}

int setCwdString(char* CwdString) {
	currentworkingDirectory = CwdString;
    return 0;
}

int freeDir(directoryEntry* directory) {
    if (directory == NULL) {
        return 0;
    }
    if (directory == getRoot()) {
        return 0;
    }
    if (directory == cwd) {
        return 0;
    }
    free(directory);
    directory = NULL;
    return 0;
}

// Key directory functions
int fs_mkdir(const char *pathname, mode_t mode){
    pathInfo pInfo;
    int result = parsePath(pathname, &pInfo);
    if(result == -1) {
        printf("Invalid Path\n");
        return -1;
    }
    if(pInfo.index != -1) {
        printf("Directory Already Exists\n");
        freeDir(pInfo.parent);
        return -1;
    }

    // Create directory entry
    directoryEntry* newDirectory = createDirectory(50, pInfo.parent);
    pInfo.index = findUnusedEntry(pInfo.parent);

    // Populate directory entry
    strcpy(pInfo.parent[pInfo.index].filename, pInfo.name);
    pInfo.parent[pInfo.index].filesize = newDirectory[0].filesize;
    copyExtents(newDirectory[0].extents, pInfo.parent[pInfo.index].extents);
    pInfo.parent[pInfo.index].isDirectory = 50;
	pInfo.parent[pInfo.index].createdDatetime = time(0);
	pInfo.parent[pInfo.index].accessedDatetime = time(0);
	pInfo.parent[pInfo.index].modifiedDatetime = time(0);

    // Write to disk and cleanup
    writeExtents(pInfo.parent, pInfo.parent[0].extents);
    free(newDirectory);
    newDirectory = NULL;
    freeDir(pInfo.parent);
    return 0;
}


int fs_rmdir(const char *pathname){
    pathInfo pInfo;
    int result = parsePath(pathname, &pInfo);
    if(result == -1) {
        printf("Invalid Path\n");
        return -1;
    }
    if(pInfo.index < 0) {
        printf("Directory doesn't exist\n");
        return -1;
    }
    if(pInfo.index < 2) {
        printf("Directory can't be . or ..\n");
        return -1;
    }
    if(pInfo.parent[pInfo.index].filesize == -1) {
        printf("Cannot Remove Root\n");
        return -1;
    }

    // Sanitize directory, set as unused, return allocated space
    directoryEntry* delDirectory = loadDir(&pInfo.parent[pInfo.index]);
    clearDir(delDirectory);
    strcpy(pInfo.parent[pInfo.index].filename, "\0");
    returnFreeSpace(pInfo.parent[pInfo.index].extents);

    // Write to disk and cleanup
    writeExtents(pInfo.parent, pInfo.parent[0].extents);
    free(delDirectory);
    delDirectory = NULL;
    freeDir(pInfo.parent);
    return 0;

}

int fs_mvitem(const char *source, const char *destination){
    // Parse source
    pathInfo pInfoSrc;
    int resultSrc = parsePath(source, &pInfoSrc);
    if(resultSrc == -1) {
        printf("Invalid Source Path\n");
        return -1;
    }
    if(pInfoSrc.index < 0) {
        printf("Source doesn't exist\n");
        return -1;
    }
    if(pInfoSrc.parent[pInfoSrc.index].filesize == -1) {
        printf("Cannot Move Root\n");
        return -1;
    }

    // Parse destination
    pathInfo pInfoDest;
    int resultDest = parsePath(destination, &pInfoDest);
    if(resultDest == -1) {
        printf("Invalid Destination Path\n");
        return -1;
    }
    if(pInfoDest.index < 0) {
        printf("Destination Not Free\n");
        return -1;
    }

    // Load the destination directory
    directoryEntry* destDirectory;
    if(pInfoDest.parent[pInfoDest.index].filesize == -1) {
        destDirectory = getRoot();
    } else {
        destDirectory = loadDir(&pInfoDest.parent[pInfoDest.index]);
    }

    // Move the directoryEntry contents to destination
    int destIndex = findUnusedEntry(destDirectory);
    copyExtents(pInfoSrc.parent[pInfoSrc.index].extents, destDirectory[destIndex].extents);
    strcpy(destDirectory[destIndex].filename, pInfoSrc.parent[pInfoSrc.index].filename);
    destDirectory[destIndex].filesize = pInfoSrc.parent[pInfoSrc.index].filesize;
    destDirectory[destIndex].isDirectory = pInfoSrc.parent[pInfoSrc.index].isDirectory;
    pInfoSrc.parent[pInfoSrc.index].isDirectory = 0;
    strcpy(pInfoSrc.parent[pInfoSrc.index].filename, "\0");

    // Write to disk and cleanup resources
    writeExtents(destDirectory, destDirectory[0].extents);
    writeExtents(pInfoSrc.parent, pInfoSrc.parent[0].extents);
    freeDir(pInfoSrc.parent);
    freeDir(pInfoDest.parent);
    freeDir(destDirectory);
    return 0;
}

// Directory iteration functions
fdDir * fs_opendir(const char *pathname){
    pathInfo pathInfoptr;
    int result = parsePath(pathname, &pathInfoptr);

    if(result == -1){
        //Invalid path
        return NULL;
    }

    fdDir* fdDirptr = malloc(sizeof(fdDir));
    struct fs_diriteminfo* dirIteminfoPtr = malloc(sizeof(struct fs_diriteminfo)); 
    
    fdDirptr->dirEntryPosition = 0; //set directory array index position
    if (pathInfoptr.index == -2) {
        fdDirptr->directory = getRoot();
        fdDirptr->d_reclen = pathInfoptr.parent[0].isDirectory;
    } else {
        fdDirptr->directory = loadDir(&pathInfoptr.parent[pathInfoptr.index]); //set directory to iterate in
        fdDirptr->d_reclen = pathInfoptr.parent[pathInfoptr.index].isDirectory;
    }
    fdDirptr->di = dirIteminfoPtr; //initilize empty ptr for fs_diriteminfo struct, the fields will be initialized in fs_readdir

    return fdDirptr; //return fdDir struct ptr that will be passed into fs_readdir
}

struct fs_diriteminfo *fs_readdir(fdDir *dirp){
    dirp->di->d_name[0] = '\0';
    while (dirp->di->d_name[0] == '\0')
    {
        //If current directory entry position is greater than number of elements in directory, return null
        if(dirp->dirEntryPosition > dirp->d_reclen-1){
            return NULL;
        }

        dirp->di->d_reclen = sizeof(struct fs_diriteminfo);
        
        //if current directoryEntry is a directory, set filetype to directory
        if(dirp->directory[dirp->dirEntryPosition].isDirectory > 0){
            dirp->di->fileType = FT_DIRECTORY;
        }
        //else current directoryEntry is a file, set filetype to file
        else{
            dirp->di->fileType = FT_REGFILE;
        }
        strcpy(dirp->di->d_name, dirp->directory[dirp->dirEntryPosition].filename); //set the name of the current directory entry

        dirp->dirEntryPosition++; //increment directory index position to move to next directory entry next fs_readdir() call
    }
    return dirp->di; //return fs_diriteminfo ptr
}

int fs_closedir(fdDir *dirp){
    //free memory that was used for fdDir and fs_diriteminfo struct pointers
    if (dirp != NULL) {
        if (dirp->di != NULL) {
            free(dirp->di);
        }
        freeDir(dirp->directory);
        free(dirp);
        return 0;
    }
    return -1; //return error
}

// Misc directory functions
char * fs_getcwd(char *pathname, size_t size){
    if(strlen(currentworkingDirectory) > size) {
        printf("Error getting the Current Working Directory");
        return NULL;
    }
    strcpy(pathname, currentworkingDirectory);
    return pathname;

}

int fs_setcwd(char *pathname){ //linux chdir
    char* path[MAXPATH];
    int num_parts = 0;
    pathInfo setcwdPtr;
    int result = parsePath(pathname, &setcwdPtr);
    if(result == -1) {
        printf("Invalid Path\n");
        return -1;
    }
    if(setcwdPtr.index == -1) {
        printf("Directory not found\n");
        freeDir(setcwdPtr.parent);
        return -1;
    }
    if(setcwdPtr.parent[setcwdPtr.index].isDirectory < 0) {
        printf("The path is not a directory\n");
        freeDir(setcwdPtr.parent);
        return -1;
    }
    //checking to see if index exists and is an element
    if(setcwdPtr.index >= 0) {
        directoryEntry* cwdptr;
        if (setcwdPtr.parent[setcwdPtr.index].filesize == -1) {
            cwdptr = getRoot();
        } else {
            cwdptr = loadDir(&setcwdPtr.parent[setcwdPtr.index]);
        }
        freeDir(cwd);
        cwd = cwdptr;
        printf("The working directory is set to: %s\n", pathname);
    } else {
        printf("Error setting the cwd\n");
        return -1;
    }
    freeDir(setcwdPtr.parent);
    //portion to update the currentworkingDirectory string
    //checking if its an absolute or relative path
    if(pathname[0] == '/') { //absolute path
        free(currentworkingDirectory);
        currentworkingDirectory = strdup(pathname); //just copy the name of the path itself
    } else { //relative path
        char* newcurrentworkingDirectory = malloc(sizeof(char)*256);
        strcpy(newcurrentworkingDirectory, currentworkingDirectory);
        strcat(newcurrentworkingDirectory, pathname);
        free(currentworkingDirectory);
        currentworkingDirectory = newcurrentworkingDirectory;
    }
    //check if the path ends in a "/"
    if(currentworkingDirectory[strlen(currentworkingDirectory)-1] != '/') {
        strcat(currentworkingDirectory, "/");
    }

    //in the case where the user cd's a relative path 
    //we need to solve the case where we need to collapse all the "./" and "../"
    //we want to create a vector without the "/" so tokenize the path
    char* savePtr = currentworkingDirectory;
    char* token = strtok_r(currentworkingDirectory, "/", &savePtr);
    while(token != NULL && num_parts < MAXPATH) {
        path[num_parts++] = strdup(token);
        token = strtok_r(NULL, "/", &savePtr); 
    }
    //now we create a integer array that will only store names of the directory path
    //not the . and ..
    int indexes[num_parts];
    int index = 0;
    for(int i = 0; i < num_parts; i++) {
        if(strcmp(path[i], ".") == 0) {
            continue;
        } else if (strcmp(path[i], "..") == 0) {
            if(index != 0) {
                index--;
            }
        } else {
            indexes[index] = i;
            index++;
        }
    }

    //copy the string that we stored in the integer array and add a "/" at the end.
    strcpy(currentworkingDirectory, "\0");
    strcat(currentworkingDirectory, "/");
    for(int i = 0; i < index; i++) {
        strcat(currentworkingDirectory, path[indexes[i]]);
        strcat(currentworkingDirectory, "/");
    }
    for(int i = 0; i < num_parts; i++) {
        free(path[i]);
        path[i] = NULL;
    }
    return 0;
}

int fs_isFile(char * filename){ //return 1 if file, 0 otherwise
    pathInfo FileInfo;
    int result = parsePath(filename, &FileInfo);
    if(result == -1) {
        return 0;
    }

    if(FileInfo.parent[FileInfo.index].isDirectory == 0) {
        freeDir(FileInfo.parent);
        return 1;
    } else {
        freeDir(FileInfo.parent);
        return 0;
    }
}

int fs_isDir(char * pathname){ //return 1 if directory, 0 otherwise
    pathInfo DirInfo;
    int result = parsePath(pathname, &DirInfo);
    if(result == -1) {
        return 0;
    }

    if(DirInfo.parent[DirInfo.index].isDirectory > 0) {
        freeDir(DirInfo.parent);
        return 1;
    } else {
        freeDir(DirInfo.parent);
        return 0;
    }
}

int fs_delete(char* filename){ //removes a file
    pathInfo pInfo;
    int result = parsePath(filename, &pInfo);
    if(result == -1) {
        return -1;
    }
    if(pInfo.index < 2) {
        printf("File doesn't exist\n");
        freeDir(pInfo.parent);
        return -1;
    }

    strcpy(pInfo.parent[pInfo.index].filename, "\0");
    returnFreeSpace(pInfo.parent[pInfo.index].extents);
    writeExtents(pInfo.parent, pInfo.parent[0].extents);
    freeDir(pInfo.parent);
    return 0;
}

int fs_stat(const char *path, struct fs_stat *buf){
    pathInfo pathInfoptr2;

    int result = parsePath(path, &pathInfoptr2);

    if(result == -1){
        return -1; //error
    }

    buf->st_size = pathInfoptr2.parent[pathInfoptr2.index].filesize;
    buf->st_blksize = BLOCK_SIZE;
    buf->st_blocks = getLastBlock(pathInfoptr2.parent[pathInfoptr2.index].extents);
    buf->st_accesstime = pathInfoptr2.parent[pathInfoptr2.index].accessedDatetime;
    buf->st_createtime = pathInfoptr2.parent[pathInfoptr2.index].modifiedDatetime;
    buf->st_createtime = pathInfoptr2.parent[pathInfoptr2.index].createdDatetime;

    freeDir(pathInfoptr2.parent);
    return 0;
}