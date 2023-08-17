# mmap

## 1. mmap(hard)

### 1.1 实验目的

`mmap`和`munmap`系统调用允许UNIX程序对其地址空间进行详细控制。它们可用于在进程之间共享内存，将文件映射到进程地址空间，并作为用户级页面错误方案的一部分，如本课程中讨论的垃圾收集算法。在本实验室中，您将把`mmap`和`munmap`添加到xv6中，重点关注内存映射文件（memory-mapped files）。

获取实验室的xv6源代码并切换到`mmap`分支：

```bash
$ git fetch
$ git checkout mmap
$ make clean
```

手册页面（运行`man 2 mmap`）显示了`mmap`的以下声明：

```c
void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset);
```

可以通过多种方式调用`mmap`，但本实验只需要与内存映射文件相关的功能子集。您可以假设`addr`始终为零，这意味着内核应该决定映射文件的虚拟地址。`mmap`返回该地址，如果失败则返回`0xffffffffffffffff`。`length`是要映射的字节数；它可能与文件的长度不同。`prot`指示内存是否应映射为可读、可写，以及/或者可执行的；您可以认为`prot`是`PROT_READ`或`PROT_WRITE`或两者兼有。`flags`要么是`MAP_SHARED`（映射内存的修改应写回文件），要么是`MAP_PRIVATE`（映射内存的修改不应写回文件）。您不必在`flags`中实现任何其他位。`fd`是要映射的文件的打开文件描述符。可以假定`offset`为零（它是要映射的文件的起点）。

允许进程映射同一个`MAP_SHARED`文件而不共享物理页面。

`munmap(addr, length)`应删除指定地址范围内的`mmap`映射。如果进程修改了内存并将其映射为`MAP_SHARED`，则应首先将修改写入文件。`munmap`调用可能只覆盖`mmap`区域的一部分，但您可以认为它取消映射的位置要么在区域起始位置，要么在区域结束位置，要么就是整个区域(但不会在区域中间“打洞”)。

 **YOUR JOB**

您应该实现足够的`mmap`和`munmap`功能，以使`mmaptest`测试程序正常工作。如果`mmaptest`不会用到某个`mmap`的特性，则不需要实现该特性。

完成后，您应该会看到以下输出：

```bash
$ mmaptest
mmap_test starting
test mmap f
test mmap f: OK
test mmap private
test mmap private: OK
test mmap read-only
test mmap read-only: OK
test mmap read/write
test mmap read/write: OK
test mmap dirty
test mmap dirty: OK
test not-mapped unmap
test not-mapped unmap: OK
test mmap two files
test mmap two files: OK
mmap_test: ALL OK
fork_test starting
fork_test OK
mmaptest: all tests succeeded
$ usertests
usertests starting
...
ALL TESTS PASSED
$
```

### 1.2 实验步骤

首先定义`vma`结构体用于保存内存映射信息，并在`proc`结构体中加入`struct vma *vma`指针：

```c
#define NVMA 16
#define VMA_START (MAXVA / 2)
struct vma{
  uint64 start;
  uint64 end;
  uint64 length; // 0 means vma not used
  uint64 off;
  int permission;
  int flags;
  struct file *file;
  struct vma *next;

  struct spinlock lock;
};

// Per-process state
struct proc {
  ...
  struct vma *vma;
  ...
};
```

之后实现对`vma`分配的代码：

```c
struct vma vma_list[NVMA];

struct vma* vma_alloc(){
  for(int i = 0; i < NVMA; i++){
    acquire(&vma_list[i].lock);
    if(vma_list[i].length == 0){
      return &vma_list[i];
    }else{
      release(&vma_list[i].lock);
    }
  }
  panic("no enough vma");
}
```

实现`mmap`系统调用，这个函数主要就是申请一个`vma`，之后查找一块空闲内存，填入相关信息，将`vma`插入到进程的`vma`链表中去：

```c
uint64
sys_mmap(void)
{
  uint64 addr;
  int length, prot, flags, fd, offset;
  if(argaddr(0, &addr) < 0 || argint(1, &length) < 0 || argint(2, &prot) < 0 || argint(3, &flags) < 0 || argint(4, &fd) < 0 || argint(5, &offset) < 0){
    return -1;
  }

  if(addr != 0)
    panic("mmap: addr not 0");
  if(offset != 0)
    panic("mmap: offset not 0");

  struct proc *p = myproc();
  struct file* f = p->ofile[fd];

  int pte_flag = PTE_U;
  if (prot & PROT_WRITE) {
    if(!f->writable && !(flags & MAP_PRIVATE)) return -1; // map to a unwritable file with PROT_WRITE
    pte_flag |= PTE_W;
  }
  if (prot & PROT_READ) {
    if(!f->readable) return -1; // map to a unreadable file with PROT_READ
    pte_flag |= PTE_R;
  }

  struct vma* v = vma_alloc();
  v->permission = pte_flag;
  v->length = length;
  v->off = offset;
  v->file = myproc()->ofile[fd];
  v->flags = flags;
  filedup(f);
  struct vma* pv = p->vma;
  if(pv == 0){
    v->start = VMA_START;
    v->end = v->start + length;
    p->vma = v;
  }else{
    while(pv->next) pv = pv->next;
    v->start = PGROUNDUP(pv->end);
    v->end = v->start + length;
    pv->next = v;
    v->next = 0;
  }
  addr = v->start;
  printf("mmap: [%p, %p)\n", addr, v->end);

  release(&v->lock);
  return addr;
}
```

接下来就可以在`usertrap`中对缺页中断进行处理：查找进程的`vma`链表，判断该地址是否为映射地址，如果不是就说明出错，直接返回；如果在`vma`链表中，就可以申请并映射一个页面，之后根据`vma`从对应的文件中读取数据：

```c
int
mmap_handler(uint64 va, int scause)
{
  struct proc *p = myproc();
  struct vma* v = p->vma;
  while(v != 0){
    if(va >= v->start && va < v->end){
      break;
    }
    //printf("%p\n", v);
    v = v->next;
  }

  if(v == 0) return -1; // not mmap addr
  if(scause == 13 && !(v->permission & PTE_R)) return -2; // unreadable vma
  if(scause == 15 && !(v->permission & PTE_W)) return -3; // unwritable vma

  // load page from file
  va = PGROUNDDOWN(va);
  char* mem = kalloc();
  if (mem == 0) return -4; // kalloc failed
  
  memset(mem, 0, PGSIZE);

  if(mappages(p->pagetable, va, PGSIZE, (uint64)mem, v->permission) != 0){
    kfree(mem);
    return -5; // map page failed
  }

  struct file *f = v->file;
  ilock(f->ip);
  readi(f->ip, 0, (uint64)mem, v->off + va - v->start, PGSIZE);
  iunlock(f->ip);
  return 0;
}
```

之后就是`munmap`的实现，同样先从链表中找到对应的`vma`结构体，之后根据三种不同情况（头部、尾部、整个）来写回并释放对应的页面并更新`vma`信息，如果整个区域都被释放就将`vma`和文件释放。

```c
uint64
sys_munmap(void)
{
  uint64 addr;
  int length;
  if(argaddr(0, &addr) < 0 || argint(1, &length) < 0){
    return -1;
  }

  struct proc *p = myproc();
  struct vma *v = p->vma;
  struct vma *pre = 0;
  while(v != 0){
    if(addr >= v->start && addr < v->end) break; // found
    pre = v;
    v = v->next;
  }

  if(v == 0) return -1; // not mapped
  printf("munmap: %p %d\n", addr, length);
  if(addr != v->start && addr + length != v->end) panic("munmap middle of vma");

  if(addr == v->start){
    writeback(v, addr, length);
    uvmunmap(p->pagetable, addr, length / PGSIZE, 1);
    if(length == v->length){
      // free all
      fileclose(v->file);
      if(pre == 0){
        p->vma = v->next; // head
      }else{
        pre->next = v->next;
        v->next = 0;
      }
      acquire(&v->lock);
      v->length = 0;
      release(&v->lock);
    }else{
      // free head
      v->start -= length;
      v->off += length;
      v->length -= length;
    }
  }else{
    // free tail
    v->length -= length;
    v->end -= length;
  }
  return 0;
}
```

写回函数先判断是否需要写回，当需要写回时就仿照`filewrite`的实现，将数据写回到对应的文件当中去，这里的实现是直接写回所有页面，但实际可以根据`PTE_D`来判断内存是否被写入，如果没有写入就不用写回：

```c
void
writeback(struct vma* v, uint64 addr, int n)
{
  if(!(v->permission & PTE_W) || (v->flags & MAP_PRIVATE)) // no need to writeback
    return;

  if((addr % PGSIZE) != 0)
    panic("unmap: not aligned");

  printf("starting writeback: %p %d\n", addr, n);

  struct file* f = v->file;

  int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
  int i = 0;
  while(i < n){
    int n1 = n - i;
    if(n1 > max)
      n1 = max;

    begin_op();
    ilock(f->ip);
    printf("%p %d %d\n",addr + i, v->off + v->start - addr, n1);
    int r = writei(f->ip, 1, addr + i, v->off + v->start - addr + i, n1);
    iunlock(f->ip);
    end_op();
    i += r;
  }
}
```

最后就是在`fork`当中复制`vma`到子进程，在`exit`中当前进程的`vma`链表释放，在`exit`时要对页面进行写回：

```c
int
fork(void)
{
  ...
  np->state = RUNNABLE;

  np->vma = 0;
  struct vma *pv = p->vma;
  struct vma *pre = 0;
  while(pv){
    struct vma *vma = vma_alloc();
    vma->start = pv->start;
    vma->end = pv->end;
    vma->off = pv->off;
    vma->length = pv->length;
    vma->permission = pv->permission;
    vma->flags = pv->flags;
    vma->file = pv->file;
    filedup(vma->file);
    vma->next = 0;
    if(pre == 0){
      np->vma = vma;
    }else{
      pre->next = vma;
    }
    pre = vma;
    release(&vma->lock);
    pv = pv->next;
  }
  ...
}

void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // munmap all mmap vma
  struct vma* v = p->vma;
  struct vma* pv;
  while(v){
    writeback(v, v->start, v->length);
    uvmunmap(p->pagetable, v->start, PGROUNDUP(v->length) / PGSIZE, 1);
    fileclose(v->file);
    pv = v->next;
    acquire(&v->lock);
    v->next = 0;
    v->length = 0;
    release(&v->lock);
    v = pv;
  }
  ...
}
```

### 1.3 实验心得

这一个实验是要实现最基础的`mmap`功能。mmap即内存映射文件，将一个文件直接映射到内存当中，之后对文件的读写就可以直接通过对内存进行读写来进行，而对文件的同步则由操作系统来负责完成。使用`mmap`可以避免对文件大量`read`和`write`操作带来的内核缓冲区和用户缓冲区之间的频繁的数据拷贝。实现mmap之后，我对于操作系统提高io效率的手段有了更深刻的认识。