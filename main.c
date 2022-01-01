#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fs.h"

char cmd[][10] = {"help", "su", "mkdir", "rmdir", "touch", "rm", "ls", "cd", "open", "close", "discard", "read", "write", "ln", "chown", "chmod", "format", "mv", "cp", "exit", "info"};

char command[100] = {0};
char dir_path[200] = {0};
char username[UNAME_LEN] = DEFAULT_USER;

void show_help()
{
    printf("命令名\t\t命令参数\t\t命令功能\n\n");
    printf("su\t\t用户名\t\t\t切换用户\n");
    printf("mkdir\t\t目录名 权限\t\t\t在当前目录创建新目录\n");
    printf("rmdir\t\t目录名\t\t\t在当前目录删除指定目录\n");
    printf("touch\t\t文件名\t\t\t新建文件\n");
    printf("rm\t\t文件名\t\t\t在当前目录下删除指定文件\n");
    printf("ls\t\t无\t\t\t显示当前目录下的目录和文件\n");
    printf("cd\t\t目录名(路径名)\t\t切换当前目录到指定目录\n");
    printf("open\t\t文件名\t\t\t在当前目录下打开指定文件\n");
    printf("close\t\t无\t\t\t在打开文件状态下，保存并关闭该文件\n");
    printf("discard\t\t无\t\t\t在打开文件状态下，不保存并关闭该文件\n");
    printf("read\t\t无\t\t\t在打开文件状态下，读取该文件\n");
    printf("write\t\t无\t\t\t在打开文件状态下，写该文件\n");
    printf("ln\t\t本文件 目标文件\t\t创建硬链接文件至目标文件\n");
    printf("chown\t\t文件名 用户名\t\t修改用户对文件的权限\n");
    printf("chmod\t\t文件名 权限\t\t修改当前文件权限\n");
    printf("format\t\t块大小 文件系统名称\t按照块大小格式化文件系统，默认直接用所有空间\n");
    printf("mv\t\t文件名 目录名\t\t将文件移动到指定目录\n");
    printf("cp\t\t文件名 目录名\t\t将文件复制到指定目录\n");
    printf("exit\t\t无\t\t\t退出系统\n\n");
}

void error(char *command)
{
    printf("%s: 缺少参数\n", command);
    printf("输入'help'来查看命令提示\n");
}

void encap_scanf(char *_pcBuffer) // Encapsulate the input function to solve the problem that scanf cannot enter spaces
{
    int iRet = 0;
    setbuf(stdin, NULL);
    fgets(_pcBuffer, 100, stdin);
    iRet = strlen(_pcBuffer);
    _pcBuffer[iRet - 1] = '\0';
}

void normalize_cmd(char *cmd) // 对输入命令的标准化：&&前后紧接着命令，没有空格，多个空格一律删除只剩下一个空格
{
    int i = 0, j = 0, Have_alpha = 0; // 判断是否为只有空格的空命令
    while (cmd[i] == ' ')             // 去掉开始的空格
    {
        i++;
    }
    while (cmd[i])
    {
        if (cmd[i] == ' ')
        {
            if (cmd[j - 1] == ' ' || cmd[j - 1] == '&') // 与符号前后不用空格，多个空格只保留一个空格
            {
                while (cmd[i] == ' ')
                    i++;
            }
            else
            {
                cmd[j++] = cmd[i++];
            }
        }
        else if (cmd[i] == '&' && cmd[i + 1] == '&')
        {
            if (cmd[j - 1] == ' ') // 如果查到与符号时，此时我们的格式化字符串最后一位为空格，那要变成与符号！！
            {
                j--;
            }
            cmd[j] = '&'; // 如果本身就是相邻的，那就正常赋值，然后i，j均向后跳跃
            cmd[j + 1] = '&';
            j += 2;
            i += 2;
        }
        else
        {
            Have_alpha = 1; // 检测到命令字母，就设置1，表示这个不是空命令！
            cmd[j++] = cmd[i++];
        }
    }
    if (cmd[j - 1] == ' ') // 这是对末尾空格的判断，如果末尾是空格，将其设置为0
    {
        cmd[j - 1] = '\0';
    }
    cmd[j] = '\0'; // 把当前为设置为0，作为终结标志
    if (Have_alpha == 0)
    {
        cmd[0] = '\0'; // 若为只有空格的空命令，就将首位设置为0
    }
}

int judge(char *sub_cmd1, char *sub_cmd2, int *ReInput)
{
    int indexOfCmd = -1, length;
    for (int i = 0; i < 21; i++) // 获取命令号下标
    {
        if (strcmp(sub_cmd1, cmd[i]) == 0)
        {
            indexOfCmd = i;
            break;
        }
    }

    int fs_ret = 0;
    char temp[200] = {0};
    FD file = 0;
    struct meta_info meta = {0};
    struct inode attr = {0};
    items_t i = 0, t = 0;
    switch (indexOfCmd) // 对命令号下标进行判别
    {
    case 0: // help
        show_help();
        break;
    case 1: // su
        fs_ret = set_user(sub_cmd2);
        if (fs_ret != 0)
        {
            printf("设置失败, 错误代码: %d\n", fs_ret);
            *ReInput = 1;
        }
        else
            memcpy(username, sub_cmd2, UNAME_LEN);
        break;
    case 2: // mkdir
        fs_ret = mkdir(sub_cmd2, 255);
        if (fs_ret != 0)
        {
            printf("失败, 错误代码: %d\n", fs_ret);
            *ReInput = 1;
        }
        break;
    case 3: // rmdir
        fs_ret = rmdir(sub_cmd2);
        if (fs_ret != 0)
        {
            printf("失败, 错误代码: %d\n", fs_ret);
            *ReInput = 1;
        }
        break;
    case 4:                                // touch
        fs_ret = create(sub_cmd2, 255, 0); // take non-softlink for granted
        if (fs_ret != 0)
        {
            printf("失败, 错误代码: %d\n", fs_ret);
            *ReInput = 1;
        }
        break;
    case 5: // rm
        fs_ret = rm(sub_cmd2);
        if (fs_ret != 0)
        {
            printf("失败, 错误代码: %d\n", fs_ret);
            *ReInput = 1;
        }
        break;
    case 6: // ls
        bl_getmeta(&meta);
        getattr(&attr, ".");
        t = attr.entry_num;
        for (i = 0; i < t; i++)
        {
            fs_ret = ls(temp, i);
            if (fs_ret != 0)
            {
                printf("失败, 错误代码: %d\n", fs_ret);
                *ReInput = 1;
            }
            getattr(&attr, temp);
            printf("名称: %s\t", temp);
            printf("类型: %d\t", attr.type); // just the type code
            printf("文件长度/子目录项个数: %d\t", attr.file_len);
            printf("引用计数(仅对文件有效): %d\t", attr.ref_count);
            printf("占用空间: %d\t", (attr.index_num + 1) * meta.block_size); // only one index block is counted for now
            printf("创建时间: %s\t", ctime(&attr.create_time));
            printf("上次访问: %s\t", ctime(&attr.last_access));
            printf("上次修改: %s\t", ctime(&attr.last_modified));
            printf("所有者: %s\t", attr.owner);
            printf("权限: %d\n", attr.permissions); // just the perm code
        }
        break;
    case 7: // cd
        fs_ret = cd_r(sub_cmd2);
        if (fs_ret != 0)
        {
            printf("失败, 错误代码: %d\n", fs_ret);
            *ReInput = 1;
        }
        break;
    case 8: // open
        fs_ret = open(&file, sub_cmd2, '+');
        if (fs_ret != 0)
        {
            printf("失败, 错误代码: %d\n", fs_ret);
            *ReInput = 1;
        }
        break;
    case 9: // close
        fs_ret = close(&file);
        if (fs_ret != 0)
        {
            printf("失败, 错误代码: %d\n", fs_ret);
            *ReInput = 1;
        }
        break;
    case 10: // discard
        fs_ret = discard(&file);
        if (fs_ret != 0)
        {
            printf("失败, 错误代码: %d\n", fs_ret);
            *ReInput = 1;
        }
        break;
    case 11: // read
        fs_ret = read(temp, &file, atoi(sub_cmd2), 199);
        if (fs_ret != 0)
        {
            printf("失败, 错误代码: %d\n", fs_ret);
            *ReInput = 1;
        }
        temp[199] = '\0';
        printf("%s", temp);
        break;
    case 12: // write
        printf("请输入内容: ");
        scanf("%s", temp);
        fs_ret = write(temp, &file, atoi(sub_cmd2), 200);
        if (fs_ret != 0)
        {
            printf("失败, 错误代码: %d\n", fs_ret);
            *ReInput = 1;
        }
        break;
    case 13:                               // ln
        fs_ret = link("target", sub_cmd2); // can use full path
        if (fs_ret != 0)
        {
            printf("失败, 错误代码: %d\n", fs_ret);
            *ReInput = 1;
        }
        break;
    case 14: // chown
        fs_ret = chperm(sub_cmd2, "root", 0, 'o');
        if (fs_ret != 0)
        {
            printf("失败, 错误代码: %d\n", fs_ret);
            *ReInput = 1;
        }
        break;
    case 15: // chmod
        fs_ret = chperm(sub_cmd2, "", 255, 'p');
        if (fs_ret != 0)
        {
            printf("失败, 错误代码: %d\n", fs_ret);
            *ReInput = 1;
        }
        break;
    case 16: // format
        fs_ret = format(atoi(sub_cmd2), VDISK_SIZE, "test");
        if (fs_ret != 0)
        {
            printf("失败, 错误代码: %d\n", fs_ret);
            *ReInput = 1;
        }
        break;
    case 17: // mv
        printf("先用cd, ln与rm手动解决吧, 真搞不完了\n");
        break;
    case 18: // cp
        printf("等于cd+create+文件操作, 写不完了\n");
        break;
    case 19: // exit
        exitfs();
        exit(0);
        break;
    case 20: // info
        bl_getmeta(&meta);
        printf("类型: %s\t卷标: %s\t块大小: %d\t卷大小: %d\t已用块数: %d\t可用块数: %d\t总块数: %d\n", meta.sys_type, meta.sys_name, meta.block_size, meta.total_size, bl_used(), bl_available(), bl_total());
        break;
    default:
        printf("%s: 找不到命令\n", sub_cmd1);
        *ReInput = 1;
        break;
    }
    return 0;
}

int main()
{
    int fs_ret = 0;
    int i = 0, index1 = 0, index2 = 0;
    int end = 0, ReInput = 0; // 判断程序是否终结。和之前命令组是否有误（比如mkdir && ls，第一个命令不对的话，第二个也要终止！）

    printf("\n\n************************ SimFS is a simulator for FS *******************************\n");
    printf("************************************************************************************\n\n");

    fs_ret = startfs();
    if (fs_ret != 0)
    {
        if (fs_ret == FS_RAW_E)
            printf("未格式化!\n", fs_ret);
        else
        {
            printf("无法启动, 错误代码: %d\n", fs_ret);
            system("pause");
            return -1;
        }
    }

    while (1)
    {
        printf("输入用户名: ");
        scanf("%s", username);
        fs_ret = set_user(username);
        if (fs_ret != 0)
        {
            printf("设置失败, 错误代码: %d\n", fs_ret);
            return -1;
        }
        else
            break;
    }

    while (1)
    {
        ReInput = 0;
        printf("%s@FileSystem$ ", username);
        encap_scanf(command);
        normalize_cmd(command);
        if (command[0] == '\0') // If it is an empty command, it will automatically re-enter the next command
            continue;

        i = 0;
        int flag = 1;                                // 1表示要输入形如cd之类的命令的处理，2表示输入相关参数
        char sub_cmd1[100] = "", sub_cmd2[100] = ""; // sub_cmd1代表着是命令，sub_cmd2代表着是相关参数
        while (1)
        {
            if (command[i] == '&' && command[i + 1] == '&') // 碰到第一个与符号，向前移动一位，可以少判断一个符号
                i++;
            else if (command[i] != ' ' && command[i] != '\0' && command[i] != '&') // 正常字符情况，对flag的情况进行判断
            {
                if (flag == 1)
                    sub_cmd1[index1++] = command[i];
                else
                    sub_cmd2[index2++] = command[i];
                i++;
            }
            else if (command[i] == ' ' && flag == 1) // 如果遍历完第一个命令后碰到了空格，就忽略空格，转为收集参数状态，命令+参数的命令格式都是这样的
            {
                i++;
                flag = 2;
            }
            else if (command[i] == '&' || command[i] == '\0') // 这些都是一个命令组执行完的情况
            {
                end = judge(sub_cmd1, sub_cmd2, &ReInput); // 对命令组判断，并且如果语法错误，复合命令的后面命令不用执行了！！
                memset(sub_cmd1, 0, sizeof(sub_cmd1));     // 对命令进行清空
                memset(sub_cmd2, 0, sizeof(sub_cmd2));
                flag = 1; // 重新变为收集参数的状态，因为之后要不就执行&&后面的下个命令组，或者多参数，要不就退出当前的输入复合命令，新开一行
                index1 = 0;
                index2 = 0;
                if (command[i] == '\0') // 表示这一行复合命令输入完了，结束
                    break;
                i++; // 指针后移
            }
            else if (command[i] == ' ' && flag == 2) // 空格可能是多参数的情况，比如make test1 test2 test3
            {
                end = judge(sub_cmd1, sub_cmd2, &ReInput); // 对命令组判断，并且如果语法错误，复合命令的后面命令不用执行了！！
                memset(sub_cmd2, 0, sizeof(sub_cmd2));
                flag = 1;
                index2 = 0; // 再继续往后收集命令或命令参数
            }
            if (ReInput == 1) // 如果语法不对，则新开一行，输入命令
                break;
        }
        if (end)
            break;
    }
    return 0;
}
