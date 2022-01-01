#ifndef FS_H
#define FS_H

#ifndef FS_TYPE
#define FS_TYPE "SimFS" // strlen should be <8 because of "char sys_type[8];"!!!
#endif
#ifndef FNAME_LEN
#define FNAME_LEN 32 // length of filename, including the length of extname
#endif
#ifndef UNAME_LEN
#define UNAME_LEN 32 // length of username
#endif
#ifndef DEFAULT_USER
#define DEFAULT_USER "root" // the length must be < UNAME_LEN
#endif
#ifndef FD_TAB_LEN
#define FD_TAB_LEN 10 // the length of FD_TAB, aka. max num of opened files
#endif
#ifndef FILE_BUF_LEN
#define FILE_BUF_LEN 1048576 // the buffer length of an opened file (in Byte)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "block.h"

typedef unsigned int items_t;
typedef unsigned char attr_t;
typedef unsigned long long ref_t;
typedef unsigned int FD; // file descriptor
#define TYPE_DATA 1
#define TYPE_DIR 2
#define TYPE_LINK_S 3

#define FS_RAW_E -7
#define FS_NAME_E -8
#define FS_FULL_E -9
#define FS_PERM_E -10
#define FS_ALLOC_E -11
#define FS_NOMATCH_E -12
#define FS_NOEMPTY_E -13
#define FS_INVOP_E -14
#define FS_FILEOPD_E -15
#define FS_FILENOP_E -16
#define FS_MAXFD_E -17
#define FS_OOF_E -18
#define FS_OOB_E -19

struct inode // aka. attributes, mod it yourself
{
    items_t index_num; // how many indices does this take, use this to calculate the actual space taken
    union
    {
        items_t entry_num; // num of entries in this directory
        struct
        {
            size_t file_len; // length of this file
            ref_t ref_count; // reference count
        };
    };
    unsigned char type; // file type

    time_t create_time, last_access, last_modified;

    char owner[UNAME_LEN];
    unsigned char permissions; // -rwx-rwx, '-' means unused
                               // the first 4 bits are for the owner
                               // the last 4 bits are for others

    // You may add some more
};

int set_user(const char *new_user);
int perm_check(const struct inode *attr, char op);
int chkname(const char *name);
int spltname(char *filename, char *path);
int startfs();
int exitfs();
int format(size_t block_size, size_t partition_size, const char *name);
int cd(const char *dirname);
int cd_r(const char *path);
int ls(char *name, items_t i);
int getattr(struct inode *attr, const char *filename);
int chperm(const char *filename, const char *owner, unsigned char permissions, char mode);
int mkdir(const char *dirname, unsigned char permissions);
int rmdir(const char *dirname);
int create(const char *filename, unsigned char permissions, unsigned char is_soft_link);
int rm(const char *filename);
int link(const char *filename, const char *target);
int open(FD *file, const char *filename, char mode);
int close(FD *file);
int discard(FD *file);
int write(const void *buf, FD *file, size_t offset, size_t size);
int read(void *buf, FD *file, size_t offset, size_t size);

#endif
