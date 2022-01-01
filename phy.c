#include "phy.h"

char **mem = NULL;
FILE **file_ctl = NULL;
size_t file_size = 0;
pfile_t file_num = 0;
FILE file_dummy;
pfile_t file_last = 0;

int qpow(int a, int n) // quick pow
{
    int ans = 1;
    while (n)
    {
        if (n & 1)
            ans *= a;
        a *= a;
        n >>= 1;
    }
    return ans;
}

int phy_start(size_t size)
{
    file_size = (size_t)qpow(2, PHY_SIZE); // to DEC
    file_num = (pfile_t)(size / file_size);
    if (size % file_size)
        file_num++;

    mem = (char **)malloc(sizeof(char *) * file_num);
    if (!mem)
        return PHY_ALLOC_E;
    file_ctl = (FILE **)malloc(sizeof(FILE *) * file_num);
    if (!file_ctl)
        return PHY_ALLOC_E;

    for (int i = 0; i < file_num; i++)
    {
        mem[i] = (char *)malloc(file_size);
        if (!mem[i])
        {
            for (i--; i >= 0; i--)
                free(mem[i]);
            free(mem);
            mem = NULL;
            return PHY_ALLOC_E;
        }
        memset(mem[i], 0, file_size); // init mem
    }

    // Reading from disk
    int ret = 0;
    char file_name[3 + PHY_MAX_FILE] = "./0\0";
    for (int i = 0; i < file_num; i++)
    {
        if (snprintf(file_name, 3 + PHY_MAX_FILE, "./%0*d", PHY_MAX_FILE, i)) // "./xxx\0"
        {
            file_ctl[i] = fopen(file_name, "rb");
            if (!file_ctl[i]) // not exists
            {
                ret = PHY_FILE_E;
                file_ctl[i] = fopen(file_name, "wb");
                if (file_ctl[i])
                {
                    fwrite(mem[i], sizeof(char), file_size, file_ctl[i]);
                    fclose(file_ctl[i]); // close for now
                    file_ctl[i] = NULL;
                }
            }
            else // exists
            {
                if (fread(mem[i], sizeof(char), file_size, file_ctl[i]) != file_size) // damaged
                    ret = PHY_FILE_E;
                fclose(file_ctl[i]); // close for now
                file_ctl[i] = NULL;
            }
        }
        else
            ret = PHY_FILE_E;
    }

    file_size--; // start from 0 now, for faster R&W
    return ret;
}

int phy_end()
{
    int ret = phy_sync(); // save to physical files
    if (ret)
        return ret;

    for (int i = 0; i < file_num; i++)
        free(mem[i]);
    free(mem);
    mem = NULL;

    free(file_ctl);
    file_ctl = NULL;

    return 0;
}

int phy_sync()
{
    int ret = 0;
    char file_name[3 + PHY_MAX_FILE] = "./0\0";

    // Saving to disk
    for (int i = 0; i < file_num; i++)
    {
        if (file_ctl[i])
        {
            snprintf(file_name, 3 + PHY_MAX_FILE, "./%0*d", PHY_MAX_FILE, i);
            file_ctl[i] = fopen(file_name, "wb");
            if (file_ctl[i])
            {
                if (fwrite(mem[i], sizeof(char), file_size + 1, file_ctl[i]) != (file_size + 1))
                    ret = PHY_FILE_E;
                fclose(file_ctl[i]); // close for now
                file_ctl[i] = NULL;
            }
            else
                ret = PHY_FILE_E;
        }
    }

    //memset(file_ctl, 0, sizeof(FILE *) * file_num); // force reset
    return ret;
}

inline char *phy_read(paddr_t addr)
{
    return &mem[addr >> PHY_SIZE][addr & file_size];
}

inline char *phy_write(paddr_t addr)
{
    pfile_t temp = (pfile_t)(addr >> PHY_SIZE);
    file_ctl[temp] = &file_dummy; // bitmap-like
    if (file_last != temp)
    {
        phy_sync();
        file_last = temp;
    }
    return &mem[temp][addr & file_size];
}
