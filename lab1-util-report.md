# Lab: Xv6 and Unix utilities

## 1. Boot xv6 (easy)

### 1.1 实验目的

搭建好开发环境，在虚拟机中成功驱动xv6操作系统。

### 1.2 实验步骤

- 克隆xv6源码

- 切换到源码目录

- 编译源码

  ```shell
  make qemu
  ```

- 编译完成后输入命令ls验证是否正确启动

### 1.3 实验心得

- 学习了基础的git命令的用法
- 学习了命令行编译以及启动xv6操作系统的方法

## 2. sleep (easy)

### 2.1 实验目的

实现xv6的UNIX程序`sleep`

### 2.2 实验步骤

- 创建`user/sleep.c`实现`sleep`程序

  ```c
  # include "kernel/types.h"
  # include "kernel/stat.h"
  # include "user/user.h"
  
  int
  main(int argc,char *argv[]){
      if (argc!=2){
          fprintf(2,"Usage: sleep times\n");
      }
      int time= atoi(*(++argv));
      //调用系统调用sleep
      if(sleep(time)!=0){
          fprintf(2,"Error in sleep sys_call!\n");
      }
      exit(0);
  }
  ```

- 在`MAKEFILE`程序中`UPROGS`添加`sleep`程序，完成之后，使用`make qemu`编译程序

- 测试程序运行是否正确

### 2.3 实验中遇到的问题和解决办法

- 不清楚如何sleep的系统调用是如何实现的。在`kernel/sysproc.c`中找到了sleep的具体实现
- 不知道用户程序如何实现。通过查看其他用户程序的实现方法，借鉴写出了`sleep`的用户程序

### 2.4 实验心得

本次实验通过实现一个简单的`sleep`程序，学会了如何实现用户程序以及如何调用系统调用，用户程序在用户态执行，通过`user/usys.S`来实现系统调用的内核态到用户态的转移，通过此程序了解了用户程序的工作流程。

## 3. pingpong (easy)

### 3.1 实验目的

编写一个使用UNIX系统调用的程序来在两个进程之间“ping-pong”一个字节，请使用两个管道，每个方向一个。父进程应该向子进程发送一个字节;子进程应该打印“`<pid>: received ping`”，其中`<pid>`是进程ID，并在管道中写入字节发送给父进程，然后退出;父级应该从读取从子进程而来的字节，打印“`<pid>: received pong`”，然后退出。您的解决方案应该在文件`user/pingpong.c`中。

### 3.2 实验步骤

- 创建`user/pingpong.c`实现`sleep`程序

  ```c
  #include "kernel/types.h"
  #include "kernel/stat.h"
  #include "user/user.h"
  
  int
  main(int argc, char* argv[])
  {
      // parent to child
      int fd[2];
  
      if (pipe(fd) == -1) {
          fprintf(2, "Error: pipe(fd) error.\n");
      }
      // child process
      if (fork() == 0){
          char buffer[1];
          read(fd[0], buffer, 1);
          close(fd[0]);
          fprintf(0, "%d: received ping\n", getpid());
          write(fd[1], buffer, 1);
      }
          // parent process
      else {
          char buffer[1];
          buffer[0] = 'a';
          write(fd[1], buffer, 1);
          close(fd[1]);
          read(fd[0], buffer, 1);
          fprintf(0, "%d: received pong\n", getpid());
  
      }
      exit(0);
  }
  ```

- 在`MAKEFILE`程序中`UPROGS`添加`pingpong`程序，完成之后，使用`make qemu`编译程序

- 测试程序是否运行正确

### 3.3 实验中遇到的问题和解决办法

- 不知道如何创建子进程，经过查阅得知使用`fork`创建子进程
- 不知道如何在不同进程中传递信息，经过查阅得知使用`pipe`来在不同进程中传递信息

### 3.4 实验心得

通过本次实验得知了如何运行使用fork函数创建子进程，以及学习了简单的多进程程序编写，同时学会了如何使用管道在不同进程中传递信息。

## 4. primes (hard)

### 4.1 实验目的

本实验要求我们实现一个求素数的程序，使用多进程的方式来提高程序的运行效率。

### 4.2 实验思路

本实验的主要思路是将素数筛选的任务分配给多个进程，每个进程负责筛选一部分数字，最后将所有进程筛选出的素数合并起来输出。

具体实现步骤如下：

- 主进程读取用户输入的数字范围，并将范围分配给每个子进程。
- 每个子进程使用埃氏筛法（Sieve of Eratosthenes）筛选出一部分素数。
- 每个子进程将筛选出的素数通过管道传递给主进程。
- 主进程将所有子进程传递过来的素数合并起来，并输出结果。

核心代码如下：

```c
#include "kernel/types.h"

#include "kernel/stat.h"
#include "user/user.h"

void f(int *left_pipe) {
  int pid = fork();
  if (pid == 0) {
    // 子进程
    int p;
    if (read(left_pipe[0], &p, sizeof(p)) == 0) {
      close(left_pipe[0]);
      return;
    }
    printf("prime %d\n", p);

    int right_pipe[2];
    pipe(right_pipe);

    while (1) {
      int n;
      int flag = read(left_pipe[0], &n, sizeof(n));
      if (n % p != 0) {
        write(right_pipe[1], &n, sizeof(n));
      }
      if (flag == 0)
        break;
    }
    close(left_pipe[0]);
    close(right_pipe[1]);
    f(right_pipe);
  } else {
    int st;
    wait(&st);
  }
}

int main() {
  int p[2];
  pipe(p);
  for (int i = 2; i <= 35; ++i) {
    write(p[1], &i, sizeof(i));
  }
  close(p[1]);
  f(p);
  exit(0);
}
```

### 4.3 实验中遇到的问题和解决办法

在实现本实验的过程中，我们遇到了一些问题，主要包括：

如何将任务分配给多个进程：我们使用了主进程将任务分配给每个子进程的方式来解决这个问题。
如何将子进程筛选出的素数传递给主进程：我们使用了管道来传递数据的方式来解决这个问题。

### 4.5 实验心得

通过本次实验，我们学习了如何使用多进程的方式来提高程序的运行效率，同时也学习了如何使用管道来在不同进程之间传递数据。这些知识对于我们编写高效的程序非常有帮助。

## 5. find (moderate)

### 5.1 实验目的

编写一个简单版本的 UNIX 查找程序：查找目录树中带有特定名称的所有文件。

### 5.2 实验步骤

本实验的主要思路是使用递归的方式遍历目录树，查找带有特定名称的文件，并将结果输出。

具体实现步骤如下：

1. 主函数读取用户输入的目录和文件名，并调用递归函数进行查找。
2. 递归函数遍历当前目录下的所有文件和子目录，如果找到了符合条件的文件，则将其路径输出。
3. 递归函数对于每个子目录，都递归调用自身进行查找。

核心代码如下：

```c
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
```

### 5.3 实验结果

经过实验，我们成功地实现了一个简单版本的 UNIX 查找程序，可以在目录树中查找带有特定名称的所有文件，并将结果输出。

### 5.4 实验中遇到的问题和解决办法

在实现本实验的过程中，我们遇到了一些问题，主要包括：

- 不了解xv6的文件系统格式：我们通过查阅xv6的源代码，了解了xv6的文件系统格式。
- 如何遍历目录树：我们使用了递归的方式来遍历目录树。
- 如何判断文件名是否符合条件：我们使用了字符串匹配的方式来判断文件名是否符合条件。

### 5.5 实验心得

通过本次实验，我们学习了如何使用递归的方式来遍历目录树，并且学习了如何使用字符串匹配的方式来判断文件名是否符合条件。这些知识对于我们编写实用的文件操作程序非常有帮助。

## 6. xargs (moderate)

### 6.1 实验目的

编写一个简单版本的 UNIX xargs 程序：从标准输入中读取行数，并为每一行运行一条命令，同时将行数作为参数提供给命令。

### 6.2 实验步骤

- 定义 xargs_exec 函数，用于执行命令。
- 定义 xargs 函数，用于从标准输入中读取行数，并为每一行运行一条命令，同时将行数作为参数提供给命令。
- 在 xargs 函数中，使用 read 函数从标准输入中读取行数，并将其存储在 buf 数组中。
- 如果读取到的字符数超过了 buf 数组的大小，则输出错误信息并退出程序。
- 如果读取到的字符为换行符，则将 buf 数组中的内容作为参数传递给 xargs_exec 函数，并清空 buf 数组。
- 在 xargs_exec 函数中，使用 fork 函数创建子进程，并在子进程中使用 exec 函数执行命令。
- 在 main 函数中，调用 xargs 函数，并传递命令名称和参数列表作为参数。如果没有传递命令名称，则默认使用 echo 命令。

代码如下：

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

// 1 为打印调试信息
#define DEBUG 0

// 宏定义
#define debug(codes) if(DEBUG) {codes}

void xargs_exec(char* program, char** paraments);

void
xargs(char** first_arg, int size, char* program_name)
{
    char buf[1024];
    debug(
            for (int i = 0; i < size; ++i) {
                printf("first_arg[%d] = %s\n", i, first_arg[i]);
            }
    )
    char *arg[MAXARG];
    int m = 0;
    while (read(0, buf+m, 1) == 1) {
        if (m >= 1024) {
            fprintf(2, "xargs: arguments too long.\n");
            exit(1);
        }
        if (buf[m] == '\n') {
            buf[m] = '\0';
            debug(printf("this line is %s\n", buf);)
            memmove(arg, first_arg, sizeof(*first_arg)*size);
            // set a arg index
            int argIndex = size;
            if (argIndex == 0) {
                arg[argIndex] = program_name;
                argIndex++;
            }
            arg[argIndex] = malloc(sizeof(char)*(m+1));
            memmove(arg[argIndex], buf, m+1);
            debug(
                    for (int j = 0; j <= argIndex; ++j)
                        printf("arg[%d] = *%s*\n", j, arg[j]);
            )
            // exec(char*, char** paraments): paraments ending with zero
            arg[argIndex+1] = 0;
            xargs_exec(program_name, arg);
            free(arg[argIndex]);
            m = 0;
        } else {
            m++;
        }
    }
}

void
xargs_exec(char* program, char** paraments)
{
    if (fork() > 0) {
        wait(0);
    } else {
        debug(
                printf("child process\n");
                printf("    program = %s\n", program);

                for (int i = 0; paraments[i] != 0; ++i) {
                    printf("    paraments[%d] = %s\n", i, paraments[i]);
                }
        )
        if (exec(program, paraments) == -1) {
            fprintf(2, "xargs: Error exec %s\n", program);
        }
        debug(printf("child exit");)
    }
}

int
main(int argc, char* argv[])
{
    debug(printf("main func\n");)
    char *name = "echo";
    if (argc >= 2) {
        name = argv[1];
        debug(
                printf("argc >= 2\n");
                printf("argv[1] = %s\n", argv[1]);
        )
    }
    else {
        debug(printf("argc == 1\n");)
    }
    xargs(argv + 1, argc - 1, name);
    exit(0);
}
```

### 6.3 实验中遇到的问题和解决办法

- 不了解xargs的功能：我们通过查阅xargs的手册，了解了xargs的功能。
- 不清楚如何具体实现xargs：我们通过查阅xargs的源代码，了解了xargs的实现原理。

### 6.4 实验心得

通过本次实验，我简单实现了一个`xargs`函数,了解了`xargs`的基本原理。