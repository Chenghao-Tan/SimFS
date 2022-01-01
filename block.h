#ifndef BLOCK_H
#define BLOCK_H

#ifndef VDISK_SIZE
#define VDISK_SIZE 1048576 // simulated disk size, in Byte
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "phy.h"

typedef unsigned long long bsize_t; // must be unsigned!!!
typedef unsigned long long baddr_t; // must be unsigned!!!
typedef unsigned char bitmap_t;     // must be unsigned!!!

#define BL_SIZE_E -3
#define BL_ALLOC_E -4
#define BL_BUF_E -5
#define BL_ADDR_E -6
#define BL_FULL_E 0 // special, use 0(root block) as error code

struct meta_info // Mod it yourself plz
{
    char sys_type[8];  // 8 Byte filesystem type, should be fixed like "ext4"
    char sys_name[8];  // 8 Byte filesystem name
    size_t block_size; // in Byte
    size_t total_size; // in Byte
};

int bl_start();                                           // start block management system
int bl_end();                                             // end
int bl_sync();                                            // call it when you want to save to the disk
int bl_format(const struct meta_info *info);              // format, need custom settings
int bl_read(void *buf, baddr_t addr, size_t size);        // copy to buf, addr must be valid, size must < block_size
int bl_write(const void *buf, baddr_t addr, size_t size); // write to block, addr must be valid, size must < block_size
baddr_t bl_new();                                         // get the addr of an unused block, will return 0(root block) on failure(may be full)
void bl_free(baddr_t addr);                               // set an block unused, will not clear the data stored in actually, just make it available for bl_new()
void bl_getmeta(struct meta_info *info);                  // copy meta info to buf
bsize_t bl_used();                                        // get the num of used blocks (time consuming warning)
bsize_t bl_total();                                       // get the total num of blocks
bsize_t bl_available();                                   // simply use bl_used() and bl_total() to calculate the num of available blocks

#endif
