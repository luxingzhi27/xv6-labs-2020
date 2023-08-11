//
// Created by luxingzhi on 23-7-11.
//

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

// 去除字符串后面的空格
char*
rtrim(char* path)
{
    static char newStr[DIRSIZ+1];
    int whiteSpaceSize = 0;
    int bufSize = 0;
    for(char* p = path + strlen(path) - 1; p >= path && *p == ' '; --p) {
        ++whiteSpaceSize;
    }
    bufSize = DIRSIZ - whiteSpaceSize;
    memmove(newStr, path, bufSize);
    newStr[bufSize] = '\0';
    return newStr;
}

void find(char * path, char *file){
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, 0)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type) {
        case T_DEVICE:
        case T_FILE:
            printf("find: %s is not a path name.",path);
            close(fd);
            break;
        case T_DIR:
            if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
                printf("find: path too long\n");
                break;
            }
            // create full path
            strcpy(buf, path);
            p = buf + strlen(buf);
            *p++ = '/';
            // read dir infomation for file and dirs
            while (read(fd, &de, sizeof(de)) == sizeof de) {
                if (de.inum == 0)
                    continue;
                if (strcmp(".", rtrim(de.name)) == 0 || strcmp("..", rtrim(de.name)) == 0)
                    continue;
                // copy file name to full path
                memmove(p, de.name, DIRSIZ);
                // create a string with zero ending.
                p[DIRSIZ] = '\0';
                // stat each of files
                if (stat(buf, &st) == -1) {
                    fprintf(2, "find: cannot stat '%s'\n", buf);
                    continue;
                }
                if (st.type == T_DEVICE || st.type == T_FILE) {
                    if (strcmp(file, rtrim(de.name)) == 0) {
                        printf("%s\n", buf);
                    }
                }
                else if (st.type == T_DIR) {
                    find(buf, file);
                }
            }

    }
}

int
main(int argc,char **argv){
    if(argc!=3){
        printf("Usage: find [path] [file].\n");
        exit(0);
    }
    find(argv[1],argv[2]);
    exit(0);
}
