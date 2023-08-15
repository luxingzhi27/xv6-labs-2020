# Copy-on-Write Fork for xv6

## 1. Implement copy-on write (hard)

### 1.1 实验目的

您的任务是在xv6内核中实现copy-on-write fork。如果修改后的内核同时成功执行`cowtest`和`usertests`程序就完成了。

为了帮助测试你的实现方案，我们提供了一个名为`cowtest`的xv6程序（源代码位于***user/cowtest.c\***）。`cowtest`运行各种测试，但在未修改的xv6上，即使是第一个测试也会失败。因此，最初您将看到：

```bash
$ cowtest
simple: fork() failed
$
```

“simple”测试分配超过一半的可用物理内存，然后执行一系列的`fork()`。`fork`失败的原因是没有足够的可用物理内存来为子进程提供父进程内存的完整副本。

完成本实验后，内核应该通过`cowtest`和`usertests`中的所有测试。即：

```bash
$ cowtest
simple: ok
simple: ok
three: zombie!
ok
three: zombie!
ok
three: zombie!
ok
file: ok
ALL COW TESTS PASSED
$ usertests
...
ALL TESTS PASSED
$
```

**这是一个合理的攻克计划：**

1. 修改`uvmcopy()`将父进程的物理页映射到子进程，而不是分配新页。在子进程和父进程的PTE中清除`PTE_W`标志。
2. 修改`usertrap()`以识别页面错误。当COW页面出现页面错误时，使用`kalloc()`分配一个新页面，并将旧页面复制到新页面，然后将新页面添加到PTE中并设置`PTE_W`。
3. 确保每个物理页在最后一个PTE对它的引用撤销时被释放——而不是在此之前。这样做的一个好方法是为每个物理页保留引用该页面的用户页表数的“引用计数”。当`kalloc()`分配页时，将页的引用计数设置为1。当`fork`导致子进程共享页面时，增加页的引用计数；每当任何进程从其页表中删除页面时，减少页的引用计数。`kfree()`只应在引用计数为零时将页面放回空闲列表。可以将这些计数保存在一个固定大小的整型数组中。你必须制定一个如何索引数组以及如何选择数组大小的方案。例如，您可以用页的物理地址除以4096对数组进行索引，并为数组提供等同于***kalloc.c\***中`kinit()`在空闲列表中放置的所有页面的最高物理地址的元素数。
4. 修改`copyout()`在遇到COW页面时使用与页面错误相同的方案。

### 1.2 实验步骤

- 原始xv6中，`fork`函数通过直接对进程的地址空间完整地复制一份来实现，十分耗时。实现`cow`可以将地址空间拷贝的耗时进行延迟分散，提高操作系统的效率。

- 修改`fork`函数，`fork`函数通过`uvmcopy`进行拷贝，修改`uvmcopy`函数如下：删去`uvmcopy`中的`kalloc`函数，将父子进程页面的页表项都设置为不可写，并设置COW标志位

  ```c
  int
  uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
  {
    pte_t *pte;
    uint64 pa, i;
    uint flags;
  
    for(i = 0; i < sz; i += PGSIZE){
      if((pte = walk(old, i, 0)) == 0)
        panic("uvmcopy: pte should exist");
      if((*pte & PTE_V) == 0)
        panic("uvmcopy: page not present");
      pa = PTE2PA(*pte);
      flags = PTE_FLAGS(*pte);
  
      *pte = ((*pte) & (~PTE_W)) | PTE_COW; // set parent's page unwritable
      // printf("c: %p %p %p\n", i, ((flags & (~PTE_W)) | PTE_COW), *pte);
      // map child's page with page unwritable
      if(mappages(new, i, PGSIZE, (uint64)pa, (flags & (~PTE_W)) | PTE_COW) != 0){
        goto err;
      }
      refcnt_incr(pa, 1);
    }
    return 0;
  
   err:
    uvmunmap(new, 0, i / PGSIZE, 1);
    return -1;
  }
  ```

- 设置一个数组用于保存内存页面的引用计数，由于会涉及到并行的问题，因此也需要设置一个锁，同时定义了一些辅助函数：

  ```c
  struct {
    struct spinlock lock;
    uint counter[(PHYSTOP - KERNBASE) / PGSIZE];
  } refcnt;
  
  inline
  uint64
  pgindex(uint64 pa){
    return (pa - KERNBASE) / PGSIZE;
  }
  
  inline
  void
  acquire_refcnt(){
    acquire(&refcnt.lock);
  }
  
  inline
  void
  release_refcnt(){
    release(&refcnt.lock);
  }
  
  void
  refcnt_setter(uint64 pa, int n){
    refcnt.counter[pgindex((uint64)pa)] = n;
  }
  
  inline
  uint
  refcnt_getter(uint64 pa){
    return refcnt.counter[pgindex(pa)];
  }
  
  void
  refcnt_incr(uint64 pa, int n){
    acquire(&refcnt.lock);
    refcnt.counter[pgindex(pa)] += n;
    release(&refcnt.lock);
  }
  ```

- 修改`kfree`函数，使其只有在引用计数为1的时候释放页面，其他时候就只减少引用计数：

  ```c
  void
  kfree(void *pa)
  {
    struct run *r;
  
    // page with refcnt > 1 should not be freed
    acquire_refcnt();
    if(refcnt.counter[pgindex((uint64)pa)] > 1){
      refcnt.counter[pgindex((uint64)pa)] -= 1;
      release_refcnt();
      return;
    }
  
    if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
      panic("kfree");
  
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);
    refcnt.counter[pgindex((uint64)pa)] = 0;
    release_refcnt();
  
    r = (struct run*)pa;
  
    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  
  ```
- 修改`kalloc`函数，使其在分配页面时将引用计数也设置为1：这里注意要判断`r`是否为0，`kalloc`实现时没有当`r==0`时就返回。
  ```c
  void *
  kalloc(void)
  {
    ...
    if(r)
      memset((char*)r, 5, PGSIZE); // fill with junk
  
    if(r)
      refcnt_incr((uint64)r, 1); // set refcnt to 1
    return (void*)r;
  }
  ```

- 在`usertrap`中加入判断语句，这里只需要处理`scause==15`的情况，因为13是页面读错误，而COW是不会引起读错误的。

  ```c
  void
  usertrap(void)
  {
    ...
    } else if(r_scause() == 15){
      // page write fault
      uint64 va = r_stval();
      if(cowcopy(va) == -1){
        p->killed = 1;
      }
    } else if((which_dev = devintr()) != 0){
    ...
  }
  ```
- 在`cowcopy`函数中先判断COW标志位，当该页面是COW页面时，就可以根据引用计数来进行处理。如果计数大于1，那么就需要通过`kalloc`申请一个新页面，然后拷贝内容，之后对该页面进行映射，映射的时候清除COW标志位，设置`PTE_W`标志位；而如果引用计数等于1，那么就不需要申请新页面，只需要对这个页面的标志位进行修改就可以了：
  ```c
  int
  cowcopy(uint64 va){
    va = PGROUNDDOWN(va);
    pagetable_t p = myproc()->pagetable;
    pte_t* pte = walk(p, va, 0);
    uint64 pa = PTE2PA(*pte);
    uint flags = PTE_FLAGS(*pte);
  
    if(!(flags & PTE_COW)){
      printf("not cow\n");
      return -2; // not cow page
    }
  
    acquire_refcnt();
    uint ref = refcnt_getter(pa);
    if(ref > 1){
      // ref > 1, alloc a new page
      char* mem = kalloc_nolock();
      if(mem == 0)
        goto bad;
      memmove(mem, (char*)pa, PGSIZE);
      if(mappages(p, va, PGSIZE, (uint64)mem, (flags & (~PTE_COW)) | PTE_W) != 0){
        kfree(mem);
        goto bad;
      }
      refcnt_setter(pa, ref - 1);
    }else{
      // ref = 1, use this page directly
      *pte = ((*pte) & (~PTE_COW)) | PTE_W;
    }
    release_refcnt();
    return 0;
  
    bad:
    release_refcnt();
    return -1;
  }
  ```

### 1.3 实验心得

在本次实验中，我学习了Copy-on-Write（COW）技术的原理和实现方式。COW技术是一种内存管理技术，它可以在多个进程之间共享内存，从而提高操作系统的性能和效率。通过这个实验，我了解了如何在操作系统中实现COW，以及如何使用COW来优化内存管理。我还学会了如何使用C语言来编写操作系统的COW代码，并且通过实验验证了COW技术的正确性和有效性。
