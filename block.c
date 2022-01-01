#include "block.h"

struct info_block
{
    struct meta_info meta_info;
    size_t bitmap_len;  // in fact, for buff use
    bsize_t block_num;  // also for buff use
    paddr_t data_start; // still as buff
};

union
{
    struct info_block info;
    char dummy[sizeof(struct info_block)];
} meta;

bitmap_t *bitmap = NULL;

int bl_start()
{
    int ret = phy_start(VDISK_SIZE); // sim disk power on
    if (ret)
        return ret;

    // Reading from disk (meta)
    for (paddr_t i = 0; i < sizeof(struct info_block); i++)
        meta.dummy[i] = *phy_read(i); // read in Bytes

    if (bitmap)
        free(bitmap);
    bitmap = malloc(meta.info.bitmap_len);
    if (!bitmap)
        return BL_ALLOC_E;

    // Reading from disk (bitmap)
    for (paddr_t i = sizeof(struct info_block); i < sizeof(struct info_block) + meta.info.bitmap_len; i++)
        *((char *)bitmap + (i - sizeof(struct info_block))) = *phy_read(i); // forced read in Bytes

    // Could do some check later, idk
    return 0;
}

int bl_end()
{
    // Writing to disk (bitmap)
    for (paddr_t i = sizeof(struct info_block); i < sizeof(struct info_block) + meta.info.bitmap_len; i++)
        *phy_write(i) = *((char *)bitmap + (i - sizeof(struct info_block))); // forced write in Bytes

    if (bitmap)
        free(bitmap);
    bitmap = NULL;
    memset(&meta.info, 0, sizeof(struct info_block));

    return phy_end(); // sim disk power off
}

inline int bl_sync() // sim disk sync
{
    return phy_sync();
}

int bl_format(const struct meta_info *info)
{
    if (info->total_size <= info->block_size)
        return BL_SIZE_E;

    memcpy(&meta.info.meta_info, info, sizeof(struct meta_info));

    // Calculate disk usage
    size_t data_len = info->total_size - sizeof(struct info_block);
    meta.info.bitmap_len = (size_t)(data_len / (info->block_size * sizeof(bitmap_t) * 8 + sizeof(bitmap_t))); // (bitmap stored as an array,) now calculate in groups of sizeof(bitmap_t)*8
    meta.info.block_num = meta.info.bitmap_len * sizeof(bitmap_t) * 8;                                        // now the actual block num
    data_len -= meta.info.bitmap_len * (info->block_size * sizeof(bitmap_t) * 8 + sizeof(bitmap_t));          // as what "%" does
    if (data_len >= info->block_size + sizeof(bitmap_t))                                                      // if singular blocks fit (n<sizeof(bitmap_t)*8)
    {
        meta.info.block_num += (bsize_t)((data_len - sizeof(bitmap_t)) / info->block_size);
        meta.info.bitmap_len += sizeof(bitmap_t);
    }
    meta.info.data_start = sizeof(struct info_block) + meta.info.bitmap_len; // data area offset

    // Creating bitmap
    if (bitmap)
        free(bitmap);
    bitmap = malloc(meta.info.bitmap_len);
    if (!bitmap)
        return BL_ALLOC_E;
    memset(bitmap, 0, meta.info.bitmap_len);                                                               // all clear
    if (meta.info.block_num % (sizeof(bitmap_t) * 8))                                                      // if has invalid bits
        bitmap[meta.info.bitmap_len - 1] = (bitmap_t)(-1) >> meta.info.block_num % (sizeof(bitmap_t) * 8); // 11...111111>>xxx ==> 00...011111
    bitmap[0] = 1 << (sizeof(bitmap_t) * 8 - 1);                                                           // set root block used

    // Writing to disk
    paddr_t i = 0;
    for (; i < sizeof(struct info_block); i++)
        *phy_write(i) = meta.dummy[i]; // write in Bytes
    for (; i < sizeof(struct info_block) + meta.info.bitmap_len; i++)
        *phy_write(i) = *((char *)bitmap + (i - sizeof(struct info_block))); // forced write in Bytes

    return 0;
}

int bl_read(void *buf, baddr_t addr, size_t size)
{
    if (!buf)
        return BL_BUF_E;
    if (addr >= meta.info.block_num)
        return BL_ADDR_E;
    if (size > meta.info.meta_info.block_size)
        return BL_SIZE_E;

    paddr_t start = meta.info.data_start + meta.info.meta_info.block_size * addr;
    for (size_t i = 0; i < size; i++)
        ((char *)buf)[i] = *phy_read(start + i);

    return 0;
}

int bl_write(const void *buf, baddr_t addr, size_t size)
{
    if (!buf)
        return BL_BUF_E;
    if (addr >= meta.info.block_num)
        return BL_ADDR_E;
    if (size > meta.info.meta_info.block_size)
        return BL_SIZE_E;

    paddr_t start = meta.info.data_start + meta.info.meta_info.block_size * addr;
    size_t i = 0;
    for (; i < size; i++)
        *phy_write(start + i) = ((char *)buf)[i];
    for (; i < meta.info.meta_info.block_size; i++)
        *phy_write(start + i) = 0; // fill with 0

    return 0;
}

baddr_t bl_new()
{
    for (size_t i = 0; i < meta.info.bitmap_len / sizeof(bitmap_t); i++) // from low to high (block num)
    {
        bitmap_t temp = bitmap[i];
        if (temp != (bitmap_t)(-1)) // if not totally occupied
        {
            for (int b = 0; b < sizeof(bitmap_t) * 8; b++) // check each bit, from high to low!!!
            {
                if ((temp & 1) == 0) // found available block
                {
                    bitmap[i] |= (bitmap_t)(1 << b); // set the bit true (can also use xor)
                    return (i + 1) * sizeof(bitmap_t) * 8 - b - 1;
                }
                else
                    temp >>= 1; // next bit
            }
        }
    }
    return BL_FULL_E; // return 0(root block)
}

void bl_free(baddr_t addr)
{
    if (addr == 0)
        return;
    // May clear it first (TODO)
    size_t i = (size_t)(addr / (sizeof(bitmap_t) * 8));
    int b = addr - i * sizeof(bitmap_t) * 8; // equal to addr%(sizeof(bitmap_t) * 8)
    b = sizeof(bitmap_t) * 8 - 1 - b;        // actually, b=(sizeof(bitmap_t) * 8)-1-addr%(sizeof(bitmap_t) * 8)
    bitmap[i] &= ~(bitmap_t)(1 << b);        // set the bit free (can also use xor)
}

inline void bl_getmeta(struct meta_info *info)
{
    *info = meta.info.meta_info;
}

bsize_t bl_used()
{
    bsize_t sum = 0;
    for (size_t i = 0; i < meta.info.bitmap_len; i++)
    {
        unsigned char temp = *((unsigned char *)bitmap + i);
        sum += temp & 1; // count the last bit
        for (int b = 0; b < 7; b++)
        {
            temp >>= 1; // next bit
            sum += temp & 1;
        }
    }
    sum -= (sizeof(bitmap_t) * 8) - meta.info.block_num % (sizeof(bitmap_t) * 8); // delete the invalid bits
    return sum;
}

inline bsize_t bl_total()
{
    return meta.info.block_num;
}

inline bsize_t bl_available()
{
    return bl_total() - bl_used();
}
