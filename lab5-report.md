# xv6 lazy page allocation

## 1. Eliminate allocation from sbrk() (easy)

### 1.1 实验目的

你的首项任务是删除`sbrk(n)`系统调用中的页面分配代码（位于**sysproc.c**中的函数`sys_sbrk()`）。`sbrk(n)`系统调用将进程的内存大小增加n个字节，然后返回新分配区域的开始部分（即旧的大小）。新的`sbrk(n)`应该只将进程的大小（`myproc()->sz`）增加n，然后返回旧的大小。它不应该分配内存——因此您应该删除对`growproc()`的调用（但是您仍然需要增加进程的大小！）。

### 1.2 实验步骤

修改`sbrk`函数，使其不调用`growproc`函数进行页面分配，关键就是`p->sz += n`将堆大小增大。

```c
uint64
sys_sbrk(void)
{
  int addr;
  int n;
  if(argint(0, &n) < 0)
    return -1;

  struct proc *p = myproc();
  addr = p->sz;
  p->sz += n;
  if(n < 0) {
    p->sz = uvmdealloc(p->pagetable, addr, addr + n);
  }
  // if(growproc(n) < 0)
  //  return -1;
  return addr;
}
```

### 1.3 实验心得

这个实验比较简单，通过修改`sbrk`函数，使其不调用`growproc`函数进行页面分配，关键就是`p->sz += n`将堆大小增大。

## 2.  Lazy allocation

### 2.1 实验目的

修改**trap.c**中的代码以响应来自用户空间的页面错误，方法是新分配一个物理页面并映射到发生错误的地址，然后返回到用户空间，让进程继续执行。您应该在生成“`usertrap(): …`”消息的`printf`调用之前添加代码。你可以修改任何其他xv6内核代码，以使`echo hi`正常工作。

### 2.2 实验步骤

**提示：**

- 你可以在`usertrap()`中查看`r_scause()`的返回值是否为13或15来判断该错误是否为页面错误
- `stval`寄存器中保存了造成页面错误的虚拟地址，你可以通过`r_stval()`读取
- 参考***vm.c\***中的`uvmalloc()`中的代码，那是一个`sbrk()`通过`growproc()`调用的函数。你将需要对`kalloc()`和`mappages()`进行调用
- 使用`PGROUNDDOWN(va)`将出错的虚拟地址向下舍入到页面边界
- 当前`uvmunmap()`会导致系统`panic`崩溃；请修改程序保证正常运行
- 如果内核崩溃，请在***kernel/kernel.asm\***中查看`sepc`
- 使用pgtbl lab的`vmprint`函数打印页表的内容
- 如果您看到错误“incomplete type proc”，请include“spinlock.h”然后是“proc.h”。

当系统发生缺页异常时，就会进入到`usertrap`函数中，此时`scause`寄存器保存的是异常原因（13为page load fault，15为page write fault），`stval`是引发缺页异常的地址。

在`usertrap`判断`scause`为13或15后，就可以读取`stval`获取引发异常的地址，之后调用`lazy_alloc`对该地址的页面进行分配即可。在这里不需要进行`p->trapframe->epc += 4`操作，因为我们要返回发生异常的那条指令并重新执行。

```c
void
usertrap(void)
{
  ...
  } else if((which_dev = devintr()) != 0){
    // ok
  } else if (r_scause() == 13 || r_scause() == 15) {
    // 13: page load fault; 15: page write fault
    // printf("page fault\n");
    uint64 addr = r_stval();
    if (lazy_alloc(addr) < 0) {
      p->killed = 1;
    }
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }
  ...
}
```

在`lazy_alloc`函数中，首先判断地址是否合法，之后通过`PGROUNDDOWN`宏获取对应页面的起始地址，然后调用`kalloc`分配页面，`memset`将页面内容置0，最后调用`mappages`将页面映射到页表中去。

```c
int
lazy_alloc(uint64 addr) {
  struct proc *p = myproc();
  // page-faults on a virtual memory address higher than any allocated with sbrk()
  // this should be >= not > !!!
  if (addr >= p->sz) {
    // printf("lazy_alloc: access invalid address");
    return -1;
  }

  if (addr < p->trapframe->sp) {
    // printf("lazy_alloc: access address below stack");
    return -2;
  }
  
  uint64 pa = PGROUNDDOWN(addr);
  char* mem = kalloc();
  if (mem == 0) {
    // printf("lazy_alloc: kalloc failed");
    return -3;
  }
  
  memset(mem, 0, PGSIZE);
  if(mappages(p->pagetable, pa, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
    kfree(mem);
    return -4;
  }
  return 0;
}
```

### 2.3 实验心得

在本次实验中，我学习了如何实现操作系统的惰性分配机制。通过修改操作系统内核代码，我成功地实现了惰性分配机制，在实现惰性分配机制的过程中，我遇到了一些困难，例如理解页面错误处理机制的工作原理和修改内核代码的方法。最终通过查阅一些资料，我成功地完成了本次实验。

## 3. Lazytests and Usertests (moderate)

### 3.1 实验目的

我们为您提供了`lazytests`，这是一个xv6用户程序，它测试一些可能会给您的惰性内存分配器带来压力的特定情况。修改内核代码，使所有`lazytests`和`usertests`都通过。

- 处理`sbrk()`参数为负的情况。
- 如果某个进程在高于`sbrk()`分配的任何虚拟内存地址上出现页错误，则终止该进程。
- 在`fork()`中正确处理父到子内存拷贝。
- 处理这种情形：进程从`sbrk()`向系统调用（如`read`或`write`）传递有效地址，但尚未分配该地址的内存。
- 正确处理内存不足：如果在页面错误处理程序中执行`kalloc()`失败，则终止当前进程。
- 处理用户栈下面的无效页面上发生的错误。

如果内核通过`lazytests`和`usertests`，那么您的解决方案是可以接受的：

```shell
$ lazytests
lazytests starting
running test lazy alloc
test lazy alloc: OK
running test lazy unmap...
usertrap(): ...
test lazy unmap: OK
running test out of memory
usertrap(): ...
test out of memory: OK
ALL TESTS PASSED
$ usertests
...
ALL TESTS PASSED
$
```

### 3.2 实验步骤

- 处理`sbrk()`参数为负数的情况，参考之前`sbrk()`调用的`growproc()`程序，如果为负数，就调用`uvmdealloc()`函数，但需要限制缩减后的内存空间不能小于0

  ```c
  uint64
  sys_sbrk(void)
  {
    int addr;
    int n;
  
    if(argint(0, &n) < 0)
      return -1;
  
    struct proc* p = myproc();
    addr = p->sz;
    uint64 sz = p->sz;
  
    if(n > 0) {
      // lazy allocation
      p->sz += n;
    } else if(sz + n > 0) {
      sz = uvmdealloc(p->pagetable, sz, sz + n);
      p->sz = sz;
    } else {
      return -1;
    }
    return addr;
  }
  ```

- 正确处理`fork`的内存拷贝：`fork`调用了`uvmcopy`进行内存拷贝，所以修改`uvmcopy`如下

  ```c
  int
  uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
  {
    ...
    for(i = 0; i < sz; i += PGSIZE){
      if((pte = walk(old, i, 0)) == 0)
        continue;
      if((*pte & PTE_V) == 0)
        continue;
      ...
    }
    ...
  }
  ```

- 修改`uvmunmap`

  ```c
  void
  uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
  {
    ...
  
    for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
      if((pte = walk(pagetable, a, 0)) == 0)
        continue;
      if((*pte & PTE_V) == 0)
        continue;
  
      ...
    }
  }
  ```

- 处理通过sbrk申请内存后还未实际分配就传给系统调用使用的情况，系统调用的处理会陷入内核，scause寄存器存储的值是8，如果此时传入的地址还未实际分配，就不能走到上文usertrap中判断scause是13或15后进行内存分配的代码，syscall执行就会失败

  - 系统调用流程：
    - 陷入内核**==>**`usertrap`中`r_scause()==8`的分支**==>**`syscall()`**==>**回到用户空间
  - 页面错误流程：
    - 陷入内核**==>**`usertrap`中`r_scause()==13||r_scause()==15`的分支**==>**分配内存**==>**回到用户空间

  因此就需要找到在何时系统调用会使用这些地址，将地址传入系统调用后，会通过`argaddr`函数(**kernel/syscall.c**)从寄存器中读取，因此在这里添加物理内存分配的代码
  
  ```c
  int
  argaddr(int n, uint64 *ip)
  {
    *ip = argraw(n);
    struct proc* p = myproc();
  
    // 处理向系统调用传入lazy allocation地址的情况
    if(walkaddr(p->pagetable, *ip) == 0) {
      if(PGROUNDUP(p->trapframe->sp) - 1 < *ip && *ip < p->sz) {
        char* pa = kalloc();
        if(pa == 0)
          return -1;
        memset(pa, 0, PGSIZE);
  
        if(mappages(p->pagetable, PGROUNDDOWN(*ip), PGSIZE, (uint64)pa, PTE_R | PTE_W | PTE_X | PTE_U) != 0) {
          kfree(pa);
          return -1;
        }
      } else {
        return -1;
      }
    }
  
    return 0;
  }
  ```

### 3.3 实验心得

本次实验对于惰性分配机制进行了测试，通过修改内核代码，使得所有`lazytests`和`usertests`都通过。让我对操作系统的页面错误处理机制和惰性分配机制有了更深入的了解。