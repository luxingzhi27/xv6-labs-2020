# page tables

## 1. Print a page table (easy)

### 1.1 实验目的

定义一个名为`vmprint()`的函数。它应当接收一个`pagetable_t`作为参数，并以下面描述的格式打印该页表。在`exec.c`中的`return argc`之前插入`if(p->pid==1) vmprint(p->pagetable)`，以打印第一个进程的页表。如果你通过了`pte printout`测试的`make grade`，你将获得此作业的满分。

```shell
page table 0x0000000087f6e000
..0: pte 0x0000000021fda801 pa 0x0000000087f6a000
.. ..0: pte 0x0000000021fda401 pa 0x0000000087f69000
.. .. ..0: pte 0x0000000021fdac1f pa 0x0000000087f6b000
.. .. ..1: pte 0x0000000021fda00f pa 0x0000000087f68000
.. .. ..2: pte 0x0000000021fd9c1f pa 0x0000000087f67000
..255: pte 0x0000000021fdb401 pa 0x0000000087f6d000
.. ..511: pte 0x0000000021fdb001 pa 0x0000000087f6c000
.. .. ..510: pte 0x0000000021fdd807 pa 0x0000000087f76000
.. .. ..511: pte 0x0000000020001c0b pa 0x0000000080007000
```

第一行显示`vmprint`的参数。之后的每行对应一个PTE，包含树中指向页表页的PTE。每个PTE行都有一些“`..`”的缩进表明它在树中的深度。每个PTE行显示其在页表页中的PTE索引、PTE比特位以及从PTE提取的物理地址。不要打印无效的PTE。在上面的示例中，顶级页表页具有条目0和255的映射。条目0的下一级只映射了索引0，该索引0的下一级映射了条目0、1和2。

您的代码可能会发出与上面显示的不同的物理地址。条目数和虚拟地址应相同。

### 1.2 实验步骤

根据实验提示，实现一个`vmprint`函数打印页表，我们只需要判断该页表为第几级页表，然后输出其内容便可。

代码如下：

```c
void printwalk(pagetable_t pagetable, uint level) {
  char* prefix;
  if (level == 2) prefix = "..";
  else if (level == 1) prefix = ".. ..";
  else prefix = ".. .. ..";

  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if(pte & PTE_V){
      uint64 pa = PTE2PA(pte);
      printf("%s%d: pte %p pa %p\n", prefix, i, pte, pa);
      if((pte & (PTE_R|PTE_W|PTE_X)) == 0){
        printwalk((pagetable_t)pa, level - 1);
      }
    }
  }
}

void
vmprint(pagetable_t pagetable) {
  printf("page table %p\n", pagetable);
  printwalk(pagetable, 2);
}
```

### 1.3 实验中遇到的困难和解决方法

实验中我不清楚如何解决判断当前页表的状态，通过提示，查阅`kernel/riscv.h`末尾处的宏，

```c
#define PTE_V (1L << 0) // valid
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4) // 1 -> user can access
```

以及查阅相关资料，得知：==在 PTE 中，低 8 位为标志位，其中 PTE_V 代表地址是否有效，当访问无效页面时会触发page fault；PTE_U代表地址能否在用户模式被访问，如果未设置则页面只能在 supervisor mode 中访问。==然后通过判断这些标志位的状态得知页表的情况，即代码中`pte & (PTE_R|PTE_W|PTE_X)`来判断当前PTE是不是指向下一级页表。

### 1.4 实验心得

通过本次实验，我学习了xv6的三级页表机制，了解了xv6的页表地址是如何转换到物理地址，即：**RISC-V 的页表逻辑上是 page table entries (PTEs) 的数组，长度为 2^27。PTE 包含 44 位物理地址号（PPN）。页的大小为 4KB，因此，分页硬件使用 39 位中的高 27 位查找 PTE，之后转化为 56 位的物理地址。**对于页表的功能也有了更深的体会，将每个进程的地址空间通过页表隔离，可以提升内存的利用率以及程序间的安全性。

## 2.  A kernel page table per process (hard)

### 2.1 实验目的

你的第一项工作是修改内核来让每一个进程在内核中执行时使用它自己的内核页表的副本。修改`struct proc`来为每一个进程维护一个内核页表，修改调度程序使得切换进程时也切换内核页表。对于这个步骤，每个进程的内核页表都应当与现有的的全局内核页表完全一致。如果你的`usertests`程序正确运行了，那么你就通过了这个实验。

### 2.2 实验步骤

1. 首先就是要建立一个函数来创建内核页表。仿照仿照`kvminit`函数，给对应的页面创建映射。

   ```c
   pagetable_t
   proc_kpagetable() {
     pagetable_t kpagetable;
     kpagetable = uvmcreate();
     if(kpagetable == 0)
       return 0;
   
     ukvmmap(kpagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);
     ukvmmap(kpagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
     ukvmmap(kpagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
     ukvmmap(kpagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W);
     ukvmmap(kpagetable, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);
     ukvmmap(kpagetable, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);
     ukvmmap(kpagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
   
     return kpagetable;
   }
   
   void
   ukvmmap(pagetable_t pagetable ,uint64 va, uint64 pa, uint64 sz, int perm)
   {
     if(mappages(pagetable, va, sz, pa, perm) != 0)
       panic("ukvmmap");
   }
   ```

2. 将`procinit`函数中的内核栈的映射移动到`allocproc`函数中。在`allocproc`函数中先创建一个内核页表，之后将内核栈映射到对应位置。

   ```c
   static struct proc*
   allocproc(void)
   {
     ...
     // Allocate a trapframe page.
     if((p->trapframe = (struct trapframe *)kalloc()) == 0){
       release(&p->lock);
       return 0;
     }
   
     // An empty user page table.
     p->pagetable = proc_pagetable(p);
     if(p->pagetable == 0){
       freeproc(p);
       release(&p->lock);
       return 0;
     }
   
     // create the kernel page table.
     p->kpagetable = proc_kpagetable(p);
     if(p->kpagetable == 0){
       freeproc(p);
       release(&p->lock);
       return 0;
     }
   
     // init the kernel stack.
     char *pa = kalloc();
     if(pa == 0)
       panic("kalloc");
     uint64 va = KSTACK((int) (p - proc));
     ukvmmap(p->kpagetable, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
     p->kstack = va;
   
     // Set up new context to start executing at forkret,
     // which returns to user space.
     memset(&p->context, 0, sizeof(p->context));
     p->context.ra = (uint64)forkret;
     p->context.sp = p->kstack + PGSIZE;
   
     return p;
   }
   ```

3. 最后在`freeproc`的时候对内核页表和内核栈也进行释放。

   ```c
   static void
   freeproc(struct proc *p)
   {
     if(p->trapframe)
       kfree((void*)p->trapframe);
     p->trapframe = 0;
   
     // free kstack
     pte_t *pte = walk(p->kpagetable, p->kstack, 0);
     if(pte == 0)
       panic("freeproc: free kstack");
     kfree((void*)PTE2PA(*pte));
     p->kstack = 0;
   
     if(p->pagetable)
       proc_freepagetable(p->pagetable, p->sz);
     if(p->kpagetable)
       proc_freekpagetable(p->kpagetable);
     ...
   }
   
   void
   proc_freekpagetable(pagetable_t kpagetable)
   {
     for (int i = 0; i < 512; i++) {
   		pte_t pte = kpagetable[i];
   		if (pte & PTE_V) {
   			if ((pte & (PTE_R|PTE_W|PTE_X)) == 0) {
   				uint64 child = PTE2PA(pte);
   				proc_freekpagetable((pagetable_t)child);
   			}
   		}
   	}
   	kfree((void*)kpagetable);
   }
   ```

### 2.3 实验心得

这一题是是要为每个进程分配一个独立的内核页表，而不是使用全局的内核页表。难度为困难，通过查阅资料，参考源代码中的很多写法，最终完成。

## 3. Simplify `copyin/copyinstr`(hard)

### 3.1 实验目的

内核的`copyin`函数读取用户指针指向的内存。它通过将用户指针转换为内核可以直接解引用的物理地址来实现这一点。这个转换是通过在软件中遍历进程页表来执行的。在本部分的实验中，您的工作是将用户空间的映射添加到每个进程的内核页表（上一节中创建），以允许`copyin`（和相关的字符串函数`copyinstr`）直接解引用用户指针。

将定义在**kernel/vm.c**中的`copyin`的主题内容替换为对`copyin_new`的调用（在**kernel/vmcopyin.c**中定义）；对`copyinstr`和`copyinstr_new`执行相同的操作。为每个进程的内核页表添加用户地址映射，以便`copyin_new`和`copyinstr_new`工作。如果`usertests`正确运行并且所有`make grade`测试都通过，那么你就完成了此项作业。

### 3.2 实验步骤

这一题这一个利用上一步的进程内核页表，将进程的地址空间映射到内核页表中，来简化`copy_in`操作，使得`copy_in`不需要去查找进程的页表来进行地址转换。

在XV6中，会涉及到进程页表改变的只有三个地方：`fork` `exec` `sbrk`，因此要在对进程页表改变后，将其同步到内核页表中。

代码通过循环遍历源页表和目标页表中的每个页面，将源页表中的页表项复制到目标页表中，并确保它们具有相同的物理地址和标志位。如果源页表中的页表项不存在，则抛出异常。如果目标页表中的页表项不存在，则创建一个新的页表项。

```c
// copy page table
void
ukvmcopy(pagetable_t pagetable, pagetable_t kpagetable, uint64 oldsz, uint64 newsz)
{
  pte_t *src, *dest;
  uint64 cur;

  if (newsz < oldsz)
    return;

  oldsz = PGROUNDUP(oldsz);
  for(cur = oldsz; cur < newsz; cur += PGSIZE){
    if ((src = walk(pagetable, cur, 0)) == 0)
      panic("ukvmcopy: pte not exist");
    if ((dest = walk(kpagetable, cur, 1)) == 0)
      panic("ukvmcopy: pte alloc failed");

    uint64 pa = PTE2PA(*src);
    *dest = PA2PTE(pa) | (PTE_FLAGS(*src) & (~PTE_U));
  }
}
```

### 3.3 实验心得
在本次实验中，我们学习了操作系统中的虚拟内存管理。我们了解了虚拟内存的概念、页表的结构和作用，以及如何使用页表来实现虚拟内存管理。我们还学习了如何在xv6操作系统中实现虚拟内存管理，包括如何分配和释放物理内存、如何映射虚拟地址到物理地址、如何处理缺页异常等。通过本次实验，我们深入了解了操作系统中的虚拟内存管理，这对于我们理解操作系统的工作原理和实现原理非常有帮助。


