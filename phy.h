#ifndef PHY_H
#define PHY_H

#ifndef PHY_SIZE
#define PHY_SIZE 12 // in bit, actual size will be 2^PHY_SIZE
#endif
#ifndef PHY_MAX_FILE
#define PHY_MAX_FILE 3 // actual max file num will be 10^MAX_FILE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long long paddr_t; // must be unsigned!!!
typedef unsigned int pfile_t;

#define PHY_ALLOC_E -1
#define PHY_FILE_E -2

int phy_start(size_t size);    // size: total size, return: error or 0
int phy_end();                 // return: error or 0
int phy_sync();                // return: error or 0 (write only!)
char *phy_read(paddr_t addr);  // addr: by char (no addr check!!!)
char *phy_write(paddr_t addr); // addr: by char (no addr check!!!)
#endif
