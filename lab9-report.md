# file system

## 1. Large files(moderate)

### 1.1 实验目的

在本作业中，您将增加xv6文件的最大大小。目前，xv6文件限制为268个块或`268*BSIZE`字节（在xv6中`BSIZE`为1024）。此限制来自以下事实：一个xv6 inode包含12个“直接”块号和一个“间接”块号，“一级间接”块指一个最多可容纳256个块号的块，总共12+256=268个块。

`bigfile`命令可以创建最长的文件，并报告其大小：

```bash
$ bigfile
..
wrote 268 blocks
bigfile: file is too small
$
```

测试失败，因为`bigfile`希望能够创建一个包含65803个块的文件，但未修改的xv6将文件限制为268个块。

您将更改xv6文件系统代码，以支持每个inode中可包含256个一级间接块地址的“二级间接”块，每个一级间接块最多可以包含256个数据块地址。结果将是一个文件将能够包含多达65803个块，或256*256+256+11个块（11而不是12，因为我们将为二级间接块牺牲一个直接块号）。

**预备**

`mkfs`程序创建xv6文件系统磁盘映像，并确定文件系统的总块数；此大小由***kernel/param.h\***中的`FSSIZE`控制。您将看到，该实验室存储库中的`FSSIZE`设置为200000个块。您应该在`make`输出中看到来自`mkfs/mkfs`的以下输出：

```
nmeta 70 (boot, super, log blocks 30 inode blocks 13, bitmap blocks 25) blocks 199930 total 200000
```

这一行描述了`mkfs/mkfs`构建的文件系统：它有70个元数据块（用于描述文件系统的块）和199930个数据块，总计200000个块。

如果在实验期间的任何时候，您发现自己必须从头开始重建文件系统，您可以运行`make clean`，强制`make`重建***fs.img\***。

**看什么**

磁盘索引节点的格式由***fs.h\***中的`struct dinode`定义。您应当尤其对`NDIRECT`、`NINDIRECT`、`MAXFILE`和`struct dinode`的`addrs[]`元素感兴趣。查看《XV6手册》中的图8.3，了解标准xv6索引结点的示意图。

在磁盘上查找文件数据的代码位于***fs.c\***的`bmap()`中。看看它，确保你明白它在做什么。在读取和写入文件时都会调用`bmap()`。写入时，`bmap()`会根据需要分配新块以保存文件内容，如果需要，还会分配间接块以保存块地址。

`bmap()`处理两种类型的块编号。`bn`参数是一个“逻辑块号”——文件中相对于文件开头的块号。`ip->addrs[]`中的块号和`bread()`的参数都是磁盘块号。您可以将`bmap()`视为将文件的逻辑块号映射到磁盘块号。

**你的工作**

修改`bmap()`，以便除了直接块和一级间接块之外，它还实现二级间接块。你只需要有11个直接块，而不是12个，为你的新的二级间接块腾出空间；不允许更改磁盘inode的大小。`ip->addrs[]`的前11个元素应该是直接块；第12个应该是一个一级间接块（与当前的一样）；13号应该是你的新二级间接块。当`bigfile`写入65803个块并成功运行`usertests`时，此练习完成：

```
$ bigfile
..................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................
wrote 65803 blocks
done; ok
$ usertests
...
ALL TESTS PASSED
$
```

运行`bigfile`至少需要一分钟半的时间。

### 1.2 实验步骤

本实验是要使XV6支持更大的文件。原始XV6中的文件块号`dinode.addr`是使用一个大小为12的直接块表以及一个大小为256的一级块表，即文件最大为`12+256`块。可以通过将一个直接块表中的项替换为一个二级块表来使系统支持大小为`11+256+256*256`个块的文件。

首先修改对应的宏以及`inode`定义。



```c
#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT * NINDIRECT)

struct dinode {
  ...
  uint addrs[NDIRECT+2];   // Data block addresses
};

struct inode {
  ...
  uint addrs[NDIRECT+2];   // Data block addresses
};
```

之后修改`bmap`函数，使其支持二级块表，其实就是重复一次块表的查询过程。



```c
static uint
bmap(struct inode *ip, uint bn)
{
  ...
  bn -= NINDIRECT;

  if(bn < NINDIRECT * NINDIRECT){
    // double indirect
    int idx = bn / NINDIRECT;
    int off = bn % NINDIRECT;
    if((addr = ip->addrs[NDIRECT + 1]) == 0)
      ip->addrs[NDIRECT + 1] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[idx]) == 0){
      a[idx] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);

    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[off]) == 0){
      a[off] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}
```

最后修改`itrunc`函数使其能够释放二级块表对应的块，主要就是注意一下`brelse`的调用就行了，仿照一级块表的处理就行了。



```c
void
itrunc(struct inode *ip)
{
  ...
  if(ip->addrs[NDIRECT + 1]){
    bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
    a = (uint*)bp->data;

    struct buf *bpd;
    uint* b;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j]){
        bpd = bread(ip->dev, a[j]);
        b = (uint*)bpd->data;
        for(int k = 0; k < NINDIRECT; k++){
          if(b[k])
            bfree(ip->dev, b[k]);
        }
        brelse(bpd);
        bfree(ip->dev, a[j]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT + 1]);
    ip->addrs[NDIRECT + 1] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}
```

## 2. Symbolic links(moderate)

### 2.1 实验目的

在本练习中，您将向xv6添加符号链接。符号链接（或软链接）是指按路径名链接的文件；当一个符号链接打开时，内核跟随该链接指向引用的文件。符号链接类似于硬链接，但硬链接仅限于指向同一磁盘上的文件，而符号链接可以跨磁盘设备。尽管xv6不支持多个设备，但实现此系统调用是了解路径名查找工作原理的一个很好的练习。

**你的工作**

您将实现`symlink(char *target, char *path)`系统调用，该调用在引用由`target`命名的文件的路径处创建一个新的符号链接。有关更多信息，请参阅`symlink`手册页（注：执行`man symlink`）。要进行测试，请将`symlinktest`添加到***Makefile\***并运行它。当测试产生以下输出（包括`usertests`运行成功）时，您就完成本作业了。

```bash
$ symlinktest
Start: test symlinks
test symlinks: ok
Start: test concurrent symlinks
test concurrent symlinks: ok
$ usertests
...
ALL TESTS PASSED
$
```

### 2.2 实验步骤

本实验是要实现符号链接，符号链接就是在文件中保存指向文件的路径名，在打开文件的时候根据保存的路径名再去查找实际文件。与符号链接相反的就是硬链接，硬链接是将文件的`inode`号指向目标文件的`inode`，并将引用计数加一。

`symlink`的系统调用实现起来也很简单，就是创建一个`inode`，设置类型为`T_SYMLINK`，然后向这个`inode`中写入目标文件的路径就行了。

```c
uint64
sys_symlink(void)
{
  char target[MAXPATH];
  memset(target, 0, sizeof(target));
  char path[MAXPATH];
  if(argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0){
    return -1;
  }
  
  struct inode *ip;

  begin_op();
  if((ip = create(path, T_SYMLINK, 0, 0)) == 0){
    end_op();
    return -1;
  }

  if(writei(ip, 0, (uint64)target, 0, MAXPATH) != MAXPATH){
    // panic("symlink write failed");
    return -1;
  }

  iunlockput(ip);
  end_op();
  return 0;
}
```

最后在`sys_open`中添加对符号链接的处理就行了，当模式不是`O_NOFOLLOW`的时候就对符号链接进行循环处理，直到找到真正的文件，如果循环超过了一定的次数（10），就说明可能发生了循环链接，就返回-1。这里主要就是要注意`namei`函数不会对`ip`上锁，需要使用`ilock`来上锁，而`create`则会上锁。

```c
uint64
sys_open(void)
{
  ...
  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    ...
  }

  if(ip->type == T_SYMLINK){
    if(!(omode & O_NOFOLLOW)){
      int cycle = 0;
      char target[MAXPATH];
      while(ip->type == T_SYMLINK){
        if(cycle == 10){
          iunlockput(ip);
          end_op();
          return -1; // max cycle
        }
        cycle++;
        memset(target, 0, sizeof(target));
        readi(ip, 0, (uint64)target, 0, MAXPATH);
        iunlockput(ip);
        if((ip = namei(target)) == 0){
          end_op();
          return -1; // target not exist
        }
        ilock(ip);
      }
    }
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
  ...
}
```

## 3. 实验心得

这次实验是要对文件系统修改，使其支持更大的文件以及符号链接，实验本身并不是很复杂。但文件系统可以说是XV6中最复杂的部分，整个文件系统包括了七层：文件描述符，路径名，目录，inode，日志，缓冲区，磁盘。通过这两个实验，使我对于这七层的实现原理有了更清晰的认识，对于我对操作系统的认识十分有益。
