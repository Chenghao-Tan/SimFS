#include "fs.h"

struct meta_info info = {0};
items_t IDX_N_FIRST = 0, IDX_N = 0, DIR_N = 0; // max entries/indices that one block can hold
void *current_dir = NULL;                      // buf for the first index block of current directory
baddr_t current_block = 0;                     // block to write current_dir buf back

char current_user[UNAME_LEN] = DEFAULT_USER;

struct entry
{
    char filename[FNAME_LEN]; // including extname
    baddr_t index_block;
};

struct file_desc
{
    char filename[FNAME_LEN]; // name of the opened file, reuse it to record whether FD is used (set to "\0" when not used)
    struct inode attr;        // inode buffer
    void *buf;                // buffer of opened file, its size is defined by FILE_BUF_LEN, or may be changeable in the future
    char mode;                // open mode
    unsigned char modified;   // whether is modified
    baddr_t index_block;      // first index block
    // you may add some more
} FD_TAB[FD_TAB_LEN] = {0}; // FD is the index of this table

int set_user(const char *new_user)
{
    if (strlen(new_user) > UNAME_LEN - 1)
        return FS_NAME_E;

    strcpy(current_user, new_user);
    return 0;
}

int startfs() // if this fails, you need to call end() or you may just format it
{
    int bl_ret = bl_start();
    if (bl_ret != 0)
        return bl_ret;

    bl_getmeta(&info);
    if (strcmp(info.sys_type, FS_TYPE) != 0) // not this type of fs inside
        return FS_RAW_E;

    IDX_N_FIRST = (info.block_size - sizeof(struct inode)) / sizeof(baddr_t);
    IDX_N = info.block_size / sizeof(baddr_t);
    DIR_N = info.block_size / sizeof(struct entry);

    current_block = 0;
    current_dir = malloc(info.block_size); // enough to hold one block
    if (!current_dir)
        return FS_ALLOC_E;
    memset(current_dir, 0, info.block_size);
    bl_ret = bl_read(current_dir, 0, info.block_size); // read root block
    if (bl_ret != 0)
        return bl_ret;

    ((struct inode *)current_dir)->last_access = time(NULL); // update access time
    bl_ret = bl_write(current_dir, 0, info.block_size);      // write back
    if (bl_ret != 0)
        return bl_ret;

    return 0;
}

int format(size_t block_size, size_t partition_size, const char *name)
{
    if (strlen(name) > 7 || strlen(FS_TYPE) > 7) // because of "char sys_name[8];"!!!
        return FS_NAME_E;

    struct meta_info temp = {0};
    strcpy(temp.sys_type, FS_TYPE);
    strcpy(temp.sys_name, name);
    temp.block_size = block_size;
    temp.total_size = partition_size;
    int bl_ret = bl_format(&temp);
    if (bl_ret != 0)
        return bl_ret;

    bl_getmeta(&info);
    IDX_N_FIRST = (info.block_size - sizeof(struct inode)) / sizeof(baddr_t);
    IDX_N = info.block_size / sizeof(baddr_t);
    DIR_N = info.block_size / sizeof(struct entry);
    current_block = 0;
    if (current_dir)
        free(current_dir);
    current_dir = malloc(info.block_size);
    if (!current_dir)
        return FS_ALLOC_E;
    memset(current_dir, 0, info.block_size);

    // Fill root block
    baddr_t entry_block = bl_new();
    if (!entry_block)
        return FS_FULL_E; // unlikely, keep root block empty, too

    ((struct inode *)current_dir)->index_num = 1; // only one entry block for now
    ((struct inode *)current_dir)->entry_num = 1; // entry for "."
    ((struct inode *)current_dir)->type = TYPE_DIR;
    time_t now = time(NULL);
    ((struct inode *)current_dir)->create_time = now;
    ((struct inode *)current_dir)->last_access = now;
    ((struct inode *)current_dir)->last_modified = now;
    strcpy(((struct inode *)current_dir)->owner, current_user);
    ((struct inode *)current_dir)->permissions = 255; // full access for all (1111 1111)

    baddr_t *idx_start = (baddr_t *)((struct inode *)current_dir + 1);
    idx_start[0] = entry_block;
    bl_ret = bl_write(current_dir, 0, info.block_size); // sticking to the standard, you need to write a whole block
    if (bl_ret != 0)
        return bl_ret;

    // Fill root block's first entry block
    struct entry current = {".", 0};                                // create entry for "."
    bl_ret = bl_write(&current, entry_block, sizeof(struct entry)); // sticking to the standard, size_t is for your buf limit
    if (bl_ret != 0)
        return bl_ret;

    return 0;
}

int perm_check(const struct inode *attr, char op) // >=0: 0 as deny, others as permit; <0: error
{
    unsigned char perm = attr->permissions;
    if (strcmp(attr->owner, current_user) == 0) // is the owner
    {
        switch (op)
        {
        case 'r':
            return perm & 64; // 0100 0000
        case 'w':
            return perm & 32; // 0010 0000
        case 'x':
            return perm & 16; // 0001 0000
        default:
            return FS_INVOP_E;
        }
    }
    else // not the owner
    {
        switch (op)
        {
        case 'r':
            return perm & 4; // 0000 0100
        case 'w':
            return perm & 2; // 0000 0010
        case 'x':
            return perm & 1; // 0000 0001
        default:
            return FS_INVOP_E;
        }
    }
}

int find_entry(baddr_t *entry_block, items_t *rel, const char *target_name, const void *index_block) // find target entry, or find empty slot if target is an empty string
{
    if (strlen(target_name) > FNAME_LEN - 1)
        return FS_NAME_E;

    void *buf = malloc(info.block_size);
    if (!buf)
        return FS_ALLOC_E;
    memset(buf, 0, info.block_size);
    if (strlen(target_name) == 0) // empty string
    {
        // TODO, may not need in the end
    }
    else
    {
        *rel = 0;
        *entry_block = ((baddr_t *)((struct inode *)index_block + 1))[0]; // target entry block (fixed for now)

        items_t entry_num = ((struct inode *)index_block)->entry_num;
        int bl_ret = bl_read(buf, *entry_block, info.block_size);
        if (bl_ret != 0)
        {
            free(buf);
            buf = NULL;
            return bl_ret;
        }

        for (*rel = 0; *rel < DIR_N && *rel < entry_num; (*rel)++)
        {
            if (strcmp(((struct entry *)buf)[*rel].filename, target_name) == 0)
            {
                free(buf);
                buf = NULL;
                return 0;
            }
        }
        // check other entry blocks (TODO)
    }
    free(buf);
    buf = NULL;
    return FS_NOMATCH_E;
}

int chkname(const char *name) // check whether '/' is in the name
{
    if (strrchr(name, '/') != NULL) // '/' in name is not allowed
        return FS_NAME_E;
    // you may check more chars
    return 0;
}

int spltname(char *filename, char *path) // split path to filename and path of dirs
{
    if (!filename || !path)
        return FS_INVOP_E;

    size_t len = strlen(path) + 1;
    size_t n = 0;
    for (size_t i = len - 2; i >= 0; i--) // read the string before the last '/' backwards
    {
        if (path[i] == '/')
            break;
        else
        {
            filename[n] = path[i];
            path[i] = '\0';
            n++;
        }
    }
    filename[n] = '\0';
    for (size_t i = 0; i < (size_t)(n / 2); i++)
    {
        char temp = filename[i];
        filename[i] = filename[n - 1 - i];
        filename[n - 1 - i] = temp;
    }

    return 0;
}

int cd_r(const char *path) // cd for multi-dirs
{
    baddr_t backup = current_block;
    char *temp = NULL;
    size_t len = strlen(path) + 1;
    temp = (char *)malloc(len);
    if (!temp)
        return FS_ALLOC_E;
    memcpy(temp, path, len);

    if (temp[0] == '/')
    {
        int ret = cd("/"); // cd to root
        if (ret != 0)
        {
            current_block = backup;                        // fallback
            bl_read(current_dir, backup, info.block_size); // fallback
            free(temp);
            return ret;
        }
    }

    char *dirname = strtok(temp, "/");
    while (dirname)
    {
        int ret = cd(dirname); // cd to root
        if (ret != 0)
        {
            current_block = backup;                        // fallback
            bl_read(current_dir, backup, info.block_size); // fallback
            free(temp);
            return ret;
        }
        dirname = strtok(NULL, "/");
    }
    free(temp);
    return 0;
}

int mkdir(const char *dirname, unsigned char permissions)
{
    if (strlen(dirname) > FNAME_LEN - 1)
        return FS_NAME_E;

    // Permission check
    if (perm_check((struct inode *)current_dir, 'w') == 0)
        return FS_PERM_E;
    if (perm_check((struct inode *)current_dir, 'x') == 0)
        return FS_PERM_E;

    // Name check
    if (chkname(dirname) != 0)
        return FS_NAME_E;
    baddr_t entry_block_p = 0;
    items_t entry_i_p = 0;
    int find_ret = find_entry(&entry_block_p, &entry_i_p, dirname, current_dir); // search under current directory
    if (find_ret == 0)
        return FS_NAME_E;

    // Space check (TODO)

    // Modify parent's attr
    // if then ((struct inode *)current_dir)->index_num++; (TODO)
    // if then get and mount new index block
    // if then get and mount new entry block
    entry_block_p = ((baddr_t *)((struct inode *)current_dir + 1))[0]; // the entry block to write (fixed for now)
    entry_i_p = ((struct inode *)current_dir)->entry_num;              // relative in one block, entry_i to write (fixed for now)

    ((struct inode *)current_dir)->entry_num++;
    time_t now = time(NULL);
    ((struct inode *)current_dir)->last_access = now;
    ((struct inode *)current_dir)->last_modified = now;
    int bl_ret = bl_write(current_dir, current_block, info.block_size); // write back
    if (bl_ret != 0)
        return bl_ret;

    // Modify parent's entry block
    struct entry new_dir = {0};
    strcpy(new_dir.filename, dirname);
    baddr_t index_block = bl_new();
    if (!index_block)
        return FS_FULL_E;
    new_dir.index_block = index_block;

    void *buf = malloc(info.block_size);
    if (!buf)
        return FS_ALLOC_E;
    memset(buf, 0, info.block_size);
    bl_ret = bl_read(buf, entry_block_p, info.block_size); // read entry array to buf
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    memcpy((struct entry *)buf + entry_i_p, &new_dir, sizeof(struct entry));
    bl_ret = bl_write(buf, entry_block_p, info.block_size); // write back
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }

    // Fill child's index block
    memset(buf, 0, info.block_size);
    ((struct inode *)buf)->index_num = 1;
    ((struct inode *)buf)->entry_num = 2; // "." and ".."
    ((struct inode *)buf)->type = TYPE_DIR;
    now = time(NULL);
    ((struct inode *)buf)->create_time = now;
    ((struct inode *)buf)->last_access = now;
    ((struct inode *)buf)->last_modified = now;
    strcpy(((struct inode *)buf)->owner, current_user);
    ((struct inode *)buf)->permissions = permissions;

    baddr_t entry_block_c = bl_new();
    if (!entry_block_c)
        return FS_FULL_E;
    ((baddr_t *)((struct inode *)buf + 1))[0] = entry_block_c;
    bl_ret = bl_write(buf, index_block, info.block_size); // write back
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }

    // Fill child's first entry block
    memset(buf, 0, info.block_size);
    struct entry self = {".", index_block}, parent = {"..", current_block};
    memcpy((struct entry *)buf, &self, sizeof(struct entry));
    memcpy((struct entry *)buf + 1, &parent, sizeof(struct entry));
    bl_ret = bl_write(buf, entry_block_c, info.block_size); // write back
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }

    free(buf);
    buf = NULL;
    return 0;
}

int rmdir(const char *dirname)
{
    if (strlen(dirname) > FNAME_LEN - 1)
        return FS_NAME_E;
    if (strcmp(dirname, "/") == 0) // root is unremovable
        return FS_PERM_E;
    if (strcmp(dirname, ".") == 0 || strcmp(dirname, "..") == 0) // unremovable dirs
        return FS_NAME_E;

    // Permission check
    if (perm_check((struct inode *)current_dir, 'w') == 0)
        return FS_PERM_E;
    if (perm_check((struct inode *)current_dir, 'x') == 0)
        return FS_PERM_E;

    // Find target
    baddr_t entry_block_p = 0;
    items_t entry_i_p = 0;
    int find_ret = find_entry(&entry_block_p, &entry_i_p, dirname, current_dir); // search under current directory
    if (find_ret != 0)
        return find_ret;

    // Check target
    void *buf = malloc(info.block_size);
    if (!buf)
        return FS_ALLOC_E;
    memset(buf, 0, info.block_size);
    int bl_ret = bl_read(buf, entry_block_p, info.block_size);
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    baddr_t target_block = ((struct entry *)buf)[entry_i_p].index_block;

    struct inode target_inode = {0};
    bl_ret = bl_read(&target_inode, target_block, sizeof(struct inode)); // reach the buffer limit
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    if (target_inode.type != TYPE_DIR)
    {
        free(buf);
        buf = NULL;
        return FS_NOMATCH_E;
    }
    if (target_inode.entry_num > 2) // can not delete root, so there are at least 2 entries
    {
        free(buf);
        buf = NULL;
        return FS_NOEMPTY_E;
    }

    // Modify parent's attr (and the last entry block)
    ((struct inode *)current_dir)->entry_num--;
    time_t now = time(NULL);
    ((struct inode *)current_dir)->last_access = now;
    ((struct inode *)current_dir)->last_modified = now;

    struct entry last_entry = {0};
    baddr_t entry_block_l = ((baddr_t *)((struct inode *)current_dir + 1))[0];    // last entry block (fixed for now)
    items_t entry_i_l = ((struct inode *)current_dir)->entry_num;                 // last entry (fixed for now)
    memcpy(&last_entry, ((struct entry *)buf) + entry_i_l, sizeof(struct entry)); // reuse buf because last entry block is fixed to one for now
    memset(((struct entry *)buf) + entry_i_l, 0, sizeof(struct entry));           // clear last entry slot
    bl_ret = bl_write(buf, entry_block_l, info.block_size);                       // write back
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    // if then free an entry block (TODO)
    // if then ((struct inode *)current_dir)->index_num--;
    // if then free an index block
    bl_ret = bl_write(current_dir, current_block, info.block_size); // write back
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }

    // Modify parent's entry block (which has the target entry)
    if (entry_block_p == entry_block_l && entry_i_p == entry_i_l) // target is the last entry, already erased
    {
        // do nothing
    }
    else
    {
        memcpy(((struct entry *)buf) + entry_i_p, &last_entry, sizeof(struct entry)); // copy the last entry in
        bl_ret = bl_write(buf, entry_block_p, info.block_size);                       // write back
        if (bl_ret != 0)
        {
            free(buf);
            buf = NULL;
            return bl_ret;
        }
    }

    // Delete child's entry block (because target is empty, only one exists)
    memset(buf, 0, info.block_size);
    bl_ret = bl_read(buf, target_block, info.block_size); // read target index block
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    bl_free(((baddr_t *)((struct inode *)buf + 1))[0]); // free the only entry block

    // Delete child's index block (because target is empty, only one exists)
    bl_free(target_block);

    free(buf);
    buf = NULL;
    return 0;
}

int ls(char *name, items_t i) // get the i-th file/dir's name (only show files under current directory)
{
    // Permission check for both r and x
    if (perm_check((struct inode *)current_dir, 'r') == 0)
        return FS_PERM_E;
    if (perm_check((struct inode *)current_dir, 'x') == 0)
        return FS_PERM_E;

    // Check whether i is a valid index
    if (i >= ((struct inode *)current_dir)->entry_num)
        return FS_NOMATCH_E;

    void *buf = malloc(info.block_size);
    if (!buf)
        return FS_ALLOC_E;
    memset(buf, 0, info.block_size);

    baddr_t entry_block = ((baddr_t *)((struct inode *)current_dir + 1))[0]; // calculate target entry block (fixed for now) (TODO)
    int bl_ret = bl_read(buf, entry_block, info.block_size);
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    strcpy(name, ((struct entry *)buf)[i].filename);

    free(buf);
    buf = NULL;
    return 0;
}

int cd(const char *dirname) // change current directory to child directory or root
{
    if (strlen(dirname) > FNAME_LEN - 1)
        return FS_NAME_E;

    // Special treatment for root
    baddr_t target_block = 0;
    void *buf = malloc(info.block_size);
    if (!buf)
        return FS_ALLOC_E;
    memset(buf, 0, info.block_size);
    if (strcmp(dirname, "/") == 0) // cd root
        target_block = 0;          // root block
    else
    {
        // Find target
        baddr_t entry_block_p = 0;
        items_t entry_i_p = 0;
        int find_ret = find_entry(&entry_block_p, &entry_i_p, dirname, current_dir); // search under current directory
        if (find_ret != 0)
        {
            free(buf);
            buf = NULL;
            return find_ret;
        }

        // Check target
        int bl_ret = bl_read(buf, entry_block_p, info.block_size);
        if (bl_ret != 0)
        {
            free(buf);
            buf = NULL;
            return bl_ret;
        }
        target_block = ((struct entry *)buf)[entry_i_p].index_block;
    }

    memset(buf, 0, info.block_size);
    int bl_ret = bl_read(buf, target_block, info.block_size); // read to buf
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    if (((struct inode *)buf)->type != TYPE_DIR) // must be a directory to cd
    {
        free(buf);
        buf = NULL;
        return FS_NOMATCH_E;
    }
    if (perm_check((struct inode *)buf, 'x') == 0) // permission check
    {
        free(buf);
        buf = NULL;
        return FS_PERM_E;
    }

    // Change dir
    memcpy(current_dir, buf, info.block_size);
    current_block = target_block;

    free(buf);
    buf = NULL;
    return 0;
}

int create(const char *filename, unsigned char permissions, unsigned char is_soft_link)
{
    if (strlen(filename) > FNAME_LEN - 1)
        return FS_NAME_E;

    // Permission check
    if (perm_check((struct inode *)current_dir, 'w') == 0)
        return FS_PERM_E;
    if (perm_check((struct inode *)current_dir, 'x') == 0)
        return FS_PERM_E;

    // Name check
    if (chkname(filename) != 0)
        return FS_NAME_E;
    baddr_t entry_block_p = 0;
    items_t entry_i_p = 0;
    int find_ret = find_entry(&entry_block_p, &entry_i_p, filename, current_dir); // search under current directory
    if (find_ret == 0)
        return FS_NAME_E;

    // Space check (TODO)

    // Modify parent's attr
    // if then ((struct inode *)current_dir)->index_num++; (TODO)
    // if then get and mount new index block
    // if then get and mount new entry block
    entry_block_p = ((baddr_t *)((struct inode *)current_dir + 1))[0]; // the entry block to write (fixed for now)
    entry_i_p = ((struct inode *)current_dir)->entry_num;              // relative in one block, entry_i to write (fixed for now)

    ((struct inode *)current_dir)->entry_num++;
    time_t now = time(NULL);
    ((struct inode *)current_dir)->last_access = now;
    ((struct inode *)current_dir)->last_modified = now;
    int bl_ret = bl_write(current_dir, current_block, info.block_size); // write back
    if (bl_ret != 0)
        return bl_ret;

    // Modify parent's entry block
    struct entry new_file = {0};
    strcpy(new_file.filename, filename);
    baddr_t index_block = bl_new();
    if (!index_block)
        return FS_FULL_E;
    new_file.index_block = index_block;

    void *buf = malloc(info.block_size);
    if (!buf)
        return FS_ALLOC_E;
    memset(buf, 0, info.block_size);
    bl_ret = bl_read(buf, entry_block_p, info.block_size); // read entry array to buf
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    memcpy((struct entry *)buf + entry_i_p, &new_file, sizeof(struct entry));
    bl_ret = bl_write(buf, entry_block_p, info.block_size); // write back
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }

    // Fill child's index block
    memset(buf, 0, info.block_size); // nothing in this file
    ((struct inode *)buf)->index_num = 0;
    ((struct inode *)buf)->file_len = 0;  // zero length
    ((struct inode *)buf)->ref_count = 1; // only itself
    if (is_soft_link)
        ((struct inode *)buf)->type = TYPE_LINK_S;
    else
        ((struct inode *)buf)->type = TYPE_DATA;
    now = time(NULL);
    ((struct inode *)buf)->create_time = now;
    ((struct inode *)buf)->last_access = now;
    ((struct inode *)buf)->last_modified = now;
    strcpy(((struct inode *)buf)->owner, current_user);
    ((struct inode *)buf)->permissions = permissions;

    bl_ret = bl_write(buf, index_block, info.block_size); // write back
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }

    free(buf);
    buf = NULL;
    return 0;
}

int link(const char *filename, const char *target) // hard link
{
    if (strlen(filename) > FNAME_LEN - 1)
        return FS_NAME_E;

    // Permission check
    if (perm_check((struct inode *)current_dir, 'w') == 0)
        return FS_PERM_E;
    if (perm_check((struct inode *)current_dir, 'x') == 0)
        return FS_PERM_E;

    // Name check
    if (chkname(filename) != 0)
        return FS_NAME_E;
    baddr_t entry_block_p = 0;
    items_t entry_i_p = 0;
    int find_ret = find_entry(&entry_block_p, &entry_i_p, filename, current_dir); // search under current directory
    if (find_ret == 0)
        return FS_NAME_E;

    // Space check (TODO)

    // Get the target index block addr
    baddr_t backup = current_block;
    char *temp = NULL;
    size_t len = strlen(target) + 1;
    temp = (char *)malloc(len);
    if (!temp)
        return FS_ALLOC_E;
    memcpy(temp, target, len);

    char target_name[FNAME_LEN] = {0};
    spltname(target_name, temp);
    int cd_ret = cd_r(temp);
    if (cd_ret != 0)
    {
        free(temp);
        temp = NULL;
        return cd_ret;
    }
    entry_block_p = 0;
    entry_i_p = 0;
    find_ret = find_entry(&entry_block_p, &entry_i_p, target_name, current_dir); // search under target's parent directory
    if (find_ret != 0)
    {
        free(temp);
        temp = NULL;
        return find_ret;
    }
    void *buf = malloc(info.block_size);
    if (!buf)
    {
        free(temp);
        temp = NULL;
        return FS_ALLOC_E;
    }
    memset(buf, 0, info.block_size);
    int bl_ret = bl_read(buf, entry_block_p, info.block_size);
    if (bl_ret != 0)
    {
        free(buf);
        free(temp);
        buf = NULL;
        temp = NULL;
        return bl_ret;
    }
    baddr_t target_block = ((struct entry *)buf)[entry_i_p].index_block; // get target's first index block

    free(temp);
    temp = NULL;
    current_block = backup;                                 // return to current dir
    bl_ret = bl_read(current_dir, backup, info.block_size); // return to current dir
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }

    // Modify parent's attr
    // if then ((struct inode *)current_dir)->index_num++; (TODO)
    // if then get and mount new index block
    // if then get and mount new entry block
    entry_block_p = ((baddr_t *)((struct inode *)current_dir + 1))[0]; // the entry block to write (fixed for now)
    entry_i_p = ((struct inode *)current_dir)->entry_num;              // relative in one block, entry_i to write (fixed for now)

    ((struct inode *)current_dir)->entry_num++;
    time_t now = time(NULL);
    ((struct inode *)current_dir)->last_access = now;
    ((struct inode *)current_dir)->last_modified = now;
    bl_ret = bl_write(current_dir, current_block, info.block_size); // write back
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }

    // Modify parent's entry block
    struct entry new_link = {0};
    strcpy(new_link.filename, filename);
    new_link.index_block = target_block;

    memset(buf, 0, info.block_size);
    bl_ret = bl_read(buf, entry_block_p, info.block_size); // read entry array to buf
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    memcpy((struct entry *)buf + entry_i_p, &new_link, sizeof(struct entry));
    bl_ret = bl_write(buf, entry_block_p, info.block_size); // write back
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }

    // Modify target's attr
    memset(buf, 0, info.block_size);
    bl_ret = bl_read(buf, target_block, info.block_size);
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    ((struct inode *)buf)->ref_count++;
    bl_ret = bl_write(buf, target_block, info.block_size);
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }

    free(buf);
    buf = NULL;
    return 0;
}

int open(FD *file, const char *filename, char mode) // mode: read/write/both/add/+(add with perm of read)
{
    if (strlen(filename) > FNAME_LEN - 1)
        return FS_NAME_E;
    if (mode != 'r' && mode != 'w' && mode != 'b' && mode != 'a' && mode != '+')
        return FS_INVOP_E;

    // Permission check
    if (perm_check((struct inode *)current_dir, 'x') == 0)
        return FS_PERM_E;

    // Find target
    baddr_t entry_block_p = 0;
    items_t entry_i_p = 0;
    int find_ret = find_entry(&entry_block_p, &entry_i_p, filename, current_dir); // search under current directory
    if (find_ret != 0)
        return find_ret;

    // Check target
    void *buf = malloc(info.block_size);
    if (!buf)
        return FS_ALLOC_E;
    memset(buf, 0, info.block_size);
    int bl_ret = bl_read(buf, entry_block_p, info.block_size);
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    baddr_t target_block = ((struct entry *)buf)[entry_i_p].index_block;

    memset(buf, 0, info.block_size);
    bl_ret = bl_read(buf, target_block, info.block_size); // read target's first index block to buf
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    if (((struct inode *)buf)->type != TYPE_DATA && ((struct inode *)buf)->type != TYPE_LINK_S) // may create special method to deal with soft link (TODO)
    {
        free(buf);
        buf = NULL;
        return FS_NOMATCH_E;
    }
    if (perm_check((struct inode *)buf, 'r') == 0) // check permissions
    {
        free(buf);
        buf = NULL;
        return FS_PERM_E;
    }

    // Get a new FD
    int ret = FS_MAXFD_E;
    for (*file = 0; *file < FD_TAB_LEN; *file++)
    {
        if (strcmp(FD_TAB[*file].filename, filename) == 0) // return existing FD, (or return a new FD? (TODO))
        {
            ret = FS_FILEOPD_E;
            break;
        }
        else if (strlen(FD_TAB[*file].filename) == 0)
        {
            ret = 0;
            break;
        }
    }
    if (ret == FS_MAXFD_E) // early exit
    {
        free(buf);
        buf = NULL;
        return ret;
    }

    // Open target file
    FD_TAB[*file].buf = malloc(FILE_BUF_LEN);
    if (!FD_TAB[*file].buf)
    {
        free(buf);
        buf = NULL;
        return FS_ALLOC_E;
    }
    memset(FD_TAB[*file].buf, 0, FILE_BUF_LEN);
    // TODO: read data blocks from other index blocks
    for (items_t i = 0; i < IDX_N_FIRST && i < ((struct inode *)buf)->index_num; i++)
    {
        char *start = (char *)(FD_TAB[*file].buf);
        start = start + i * info.block_size; // offset i blocks
        int bl_ret = bl_read(start, ((baddr_t *)((struct inode *)buf + 1))[i], info.block_size);
        if (bl_ret != 0)
        {
            free(buf);
            buf = NULL;
            return bl_ret;
        }
    }
    ((struct inode *)buf)->last_access = time(NULL);
    bl_ret = bl_write(buf, target_block, info.block_size); // write access time back
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }

    // Fill FD completely
    FD_TAB[*file].index_block = target_block;               // for write back
    strcpy(FD_TAB[*file].filename, filename);               // copy filename
    memcpy(&FD_TAB[*file].attr, buf, sizeof(struct inode)); // copy attr
    FD_TAB[*file].mode = mode;
    FD_TAB[*file].modified = 0;

    free(buf);
    buf = NULL;
    return ret;
}

int close(FD *file) // must cd to the parent directory to close the file, make sure no one is reading/writing. There's no checking!!!
{
    if (strlen(FD_TAB[*file].filename) == 0) // "\0", FD not used
        return FS_FILENOP_E;

    // Permission check
    if (perm_check((struct inode *)current_dir, 'x') == 0)
        return FS_PERM_E;

    // Close target file
    if (FD_TAB[*file].modified == 1) // if is modified, try to write back
    {
        baddr_t target_block = FD_TAB[*file].index_block;
        void *buf = malloc(info.block_size);
        if (!buf)
            return FS_ALLOC_E;
        memset(buf, 0, info.block_size);
        int bl_ret = bl_read(buf, target_block, info.block_size); // read target's first index block to buf
        if (bl_ret != 0)
        {
            free(buf);
            buf = NULL;
            return bl_ret;
        }

        // Write to disk
        // TODO: check whether the disk is full before actual writing
        if (perm_check((struct inode *)buf, 'w') == 0) // check permissions (read from disk because it may be changed)
        {
            free(buf);
            buf = NULL;
            return FS_PERM_E;
        }

        // TODO: write to data blocks which are from other index blocks
        items_t remain_blocks = ((struct inode *)buf)->index_num;
        for (items_t i = 0; i < IDX_N_FIRST && i < FD_TAB[*file].attr.index_num; i++)
        {
            baddr_t target = 0;
            if (remain_blocks > 0) // reuse allocated blocks
            {
                target = ((baddr_t *)((struct inode *)buf + 1))[i];
                remain_blocks--;
            }
            else // no more allocated blocks
            {
                target = bl_new();
                if (!target)
                {
                    free(buf);
                    buf = NULL;
                    return FS_FULL_E;
                }
                ((baddr_t *)((struct inode *)buf + 1))[i] = target;
            }
            char *start = (char *)(FD_TAB[*file].buf);
            start = start + i * info.block_size; // offset i blocks
            int bl_ret = bl_write(start, target, info.block_size);
            if (bl_ret != 0)
            {
                free(buf);
                buf = NULL;
                return bl_ret;
            }
        }
        if (remain_blocks > 0) // if the file is shorter now, free the extra space
        {
            for (items_t i = 0; i < remain_blocks; i++)
                bl_free(((baddr_t *)((struct inode *)buf + 1))[FD_TAB[*file].attr.index_num + i]);
        }

        FD_TAB[*file].attr.last_modified = time(NULL);          // record time of last modified
        memcpy(buf, &FD_TAB[*file].attr, sizeof(struct inode)); // copy attr
        bl_ret = bl_write(buf, target_block, info.block_size);  // read target's first index block to buf
        if (bl_ret != 0)
        {
            free(buf);
            buf = NULL;
            return bl_ret;
        }
    }

    free(FD_TAB[*file].buf);                             // discard the buf in FD
    memset(&FD_TAB[*file], 0, sizeof(struct file_desc)); // will also set filename to "\0"
    return 0;
}

int write(const void *buf, FD *file, size_t offset, size_t size) // only work with FD, without writing to disk
{
    if (FD_TAB[*file].mode != 'w' && FD_TAB[*file].mode != 'b' && FD_TAB[*file].mode != 'a' && FD_TAB[*file].mode != '+')
        return FS_INVOP_E;
    if (offset > FD_TAB[*file].attr.file_len) // out of file as well as not at the tail
        return FS_OOF_E;

    if (FD_TAB[*file].mode == 'w' || FD_TAB[*file].mode == 'b') // truncate
    {
        if (offset + size > FILE_BUF_LEN) // just in case
            return FS_OOB_E;

        FD_TAB[*file].modified = 1;
        FD_TAB[*file].attr.file_len = offset + size;                                             // update the length of file
        FD_TAB[*file].attr.index_num = (items_t)(FD_TAB[*file].attr.file_len / info.block_size); // update the num of used data blocks
        if (FD_TAB[*file].attr.file_len % info.block_size > 0)
            FD_TAB[*file].attr.index_num++;

        memcpy((char *)(FD_TAB[*file].buf) + offset, buf, size);
    }
    else if (FD_TAB[*file].mode == 'a' || FD_TAB[*file].mode == '+') // append
    {
        if (FD_TAB[*file].attr.file_len + size > FILE_BUF_LEN) // just in case
            return FS_OOB_E;

        size_t origin_len = FD_TAB[*file].attr.file_len;
        FD_TAB[*file].modified = 1;
        FD_TAB[*file].attr.file_len = origin_len + size;                                         // update the length of file
        FD_TAB[*file].attr.index_num = (items_t)(FD_TAB[*file].attr.file_len / info.block_size); // update the num of used data blocks
        if (FD_TAB[*file].attr.file_len % info.block_size > 0)
            FD_TAB[*file].attr.index_num++;

        void *temp = malloc(FILE_BUF_LEN); // for faster copy
        if (!temp)
            return FS_ALLOC_E;

        memcpy(temp, FD_TAB[*file].buf, offset);                                                         // head
        memcpy((char *)temp + offset + size, (char *)(FD_TAB[*file].buf) + offset, origin_len - offset); // tail
        memcpy((char *)temp + offset, buf, size);                                                        // mid
        memcpy(FD_TAB[*file].buf, temp, FD_TAB[*file].attr.file_len);                                    // whole

        free(temp);
        buf = NULL;
    }
    return 0;
}

int read(void *buf, FD *file, size_t offset, size_t size) // only work with FD
{
    if (FD_TAB[*file].mode != 'r' && FD_TAB[*file].mode != 'b' && FD_TAB[*file].mode != '+')
        return FS_INVOP_E;
    if (offset > FD_TAB[*file].attr.file_len) // out of file as well as not at the tail
        return FS_OOF_E;
    if (offset + size > FILE_BUF_LEN) // just in case
        return FS_OOB_E;

    memcpy(buf, (char *)(FD_TAB[*file].buf) + offset, size);
    return 0;
}

inline int discard(FD *file) // close file without save to disk, must be called under the parent directory
{
    FD_TAB[*file].modified = 0;
    return close(file);
}

int rm(const char *filename)
{
    if (strlen(filename) > FNAME_LEN - 1)
        return FS_NAME_E;

    // Permission check
    if (perm_check((struct inode *)current_dir, 'w') == 0)
        return FS_PERM_E;
    if (perm_check((struct inode *)current_dir, 'x') == 0)
        return FS_PERM_E;

    // Find target
    baddr_t entry_block_p = 0;
    items_t entry_i_p = 0;
    int find_ret = find_entry(&entry_block_p, &entry_i_p, filename, current_dir); // search under current directory
    if (find_ret != 0)
        return find_ret;

    // Check target
    void *buf = malloc(info.block_size);
    if (!buf)
        return FS_ALLOC_E;
    memset(buf, 0, info.block_size);
    int bl_ret = bl_read(buf, entry_block_p, info.block_size);
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    baddr_t target_block = ((struct entry *)buf)[entry_i_p].index_block;

    struct inode target_inode = {0};
    bl_ret = bl_read(&target_inode, target_block, sizeof(struct inode)); // reach the buffer limit
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    if (target_inode.type != TYPE_DATA && target_inode.type != TYPE_LINK_S)
    {
        free(buf);
        buf = NULL;
        return FS_NOMATCH_E;
    }

    // Modify parent's attr (and the last entry block)
    ((struct inode *)current_dir)->entry_num--;
    time_t now = time(NULL);
    ((struct inode *)current_dir)->last_access = now;
    ((struct inode *)current_dir)->last_modified = now;

    struct entry last_entry = {0};
    baddr_t entry_block_l = ((baddr_t *)((struct inode *)current_dir + 1))[0];    // last entry block (fixed for now)
    items_t entry_i_l = ((struct inode *)current_dir)->entry_num;                 // last entry (fixed for now)
    memcpy(&last_entry, ((struct entry *)buf) + entry_i_l, sizeof(struct entry)); // reuse buf because last entry block is fixed to one for now
    memset(((struct entry *)buf) + entry_i_l, 0, sizeof(struct entry));           // clear last entry slot
    bl_ret = bl_write(buf, entry_block_l, info.block_size);                       // write back
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    // if then free an entry block (TODO)
    // if then ((struct inode *)current_dir)->index_num--;
    // if then free an index block
    bl_ret = bl_write(current_dir, current_block, info.block_size); // write back
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }

    // Modify parent's entry block (which has the target entry)
    if (entry_block_p == entry_block_l && entry_i_p == entry_i_l) // target is the last entry, already erased
    {
        // do nothing
    }
    else
    {
        memcpy(((struct entry *)buf) + entry_i_p, &last_entry, sizeof(struct entry)); // copy the last entry in
        bl_ret = bl_write(buf, entry_block_p, info.block_size);                       // write back
        if (bl_ret != 0)
        {
            free(buf);
            buf = NULL;
            return bl_ret;
        }
    }

    // Deal with child's data
    memset(buf, 0, info.block_size);
    bl_ret = bl_read(buf, target_block, info.block_size); // read target index block
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    if (((struct inode *)buf)->ref_count > 1) // there are other hard links
    {
        ((struct inode *)buf)->ref_count--;
        bl_ret = bl_write(buf, target_block, info.block_size); // write back
        if (bl_ret != 0)
        {
            free(buf);
            buf = NULL;
            return bl_ret;
        }
    }
    else // is the only copy that exits
    {
        // Delete child's data blocks (many may exist)
        for (items_t i = 0; i < IDX_N_FIRST && i < ((struct inode *)buf)->index_num; i++)
            bl_free(((baddr_t *)((struct inode *)buf + 1))[i]); // free the data block one by one
        // free the other data blocks stored in other index blocks (TODO)

        // Delete child's index blocks (many may exist)
        bl_free(target_block);
        // free the other index blocks (TODO)
    }

    free(buf);
    buf = NULL;
    return 0;
}

int chperm(const char *filename, const char *owner, unsigned char permissions, char mode) // chmod or/and chown as mode set (o, p, b stand for owner, permissions, both)
{
    if (strlen(filename) > FNAME_LEN - 1)
        return FS_NAME_E;
    if (strlen(owner) > UNAME_LEN - 1)
        return FS_NAME_E;
    if (mode != 'o' && mode != 'p' && mode != 'b')
        return FS_INVOP_E;

    // Permission check
    if (perm_check((struct inode *)current_dir, 'x') == 0)
        return FS_PERM_E;

    // Find target
    baddr_t entry_block_p = 0;
    items_t entry_i_p = 0;
    int find_ret = find_entry(&entry_block_p, &entry_i_p, filename, current_dir); // search under current directory
    if (find_ret != 0)
        return find_ret;

    // Check target
    void *buf = malloc(info.block_size);
    if (!buf)
        return FS_ALLOC_E;
    memset(buf, 0, info.block_size);
    int bl_ret = bl_read(buf, entry_block_p, info.block_size);
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    baddr_t target_block = ((struct entry *)buf)[entry_i_p].index_block;

    memset(buf, 0, info.block_size);
    bl_ret = bl_read(buf, target_block, info.block_size);
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    if (strcmp(current_user, ((struct inode *)buf)->owner) != 0) // only its owner can modify the permissions
    {
        free(buf);
        buf = NULL;
        return FS_PERM_E;
    }

    // Modify target's first index block
    if (mode == 'o' || mode == 'b')
        strcpy(((struct inode *)buf)->owner, owner);
    if (mode == 'p' || mode == 'b')
        ((struct inode *)buf)->permissions = permissions;
    bl_ret = bl_write(buf, target_block, info.block_size); // write back
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }

    free(buf);
    buf = NULL;
    return 0;
}

int getattr(struct inode *attr, const char *filename) // get the file/dir's attr, aka. special verison of ls()
{
    // Permission check for both r and x
    if (perm_check((struct inode *)current_dir, 'r') == 0)
        return FS_PERM_E;
    if (perm_check((struct inode *)current_dir, 'x') == 0)
        return FS_PERM_E;

    baddr_t entry_block_p = 0;
    items_t entry_i_p = 0;
    int find_ret = find_entry(&entry_block_p, &entry_i_p, filename, current_dir); // search under current directory
    if (find_ret != 0)
        return find_ret;

    // Get target
    void *buf = malloc(info.block_size);
    if (!buf)
        return FS_ALLOC_E;
    memset(buf, 0, info.block_size);
    int bl_ret = bl_read(buf, entry_block_p, info.block_size);
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }
    baddr_t target_block = ((struct entry *)buf)[entry_i_p].index_block;

    bl_ret = bl_read(attr, target_block, sizeof(struct inode)); // reach the buffer limit
    if (bl_ret != 0)
    {
        free(buf);
        buf = NULL;
        return bl_ret;
    }

    free(buf);
    buf = NULL;
    return 0;
}

int exitfs()
{
    if (current_dir)
        free(current_dir);
    current_dir = NULL;
    return bl_end();
}
