# System calls

## 1. System call tracing (moderate)

### 1.1. 实验目的

在本作业中，您将添加一项系统调用跟踪功能，这可能会在以后的实验调试中有所帮助。您将创建一个新的跟踪系统调用来控制跟踪。它应该接受一个参数，即一个整数 "掩码"，该掩码的位数指定了要跟踪的系统调用。例如，要跟踪 `fork` 系统调用，程序会调用 `trace(1<<SYS_fork)`，其中 `SYS_fork` 是 `kernel/syscall.h` 中的系统调用编号。你必须修改 `xv6` 内核，以便在每个系统调用即将返回时，如果掩码中设置了系统调用编号，就打印出一行。该行应包含进程 ID、系统调用名称和返回值；无需打印系统调用参数。跟踪系统调用应启用对调用该调用的进程及其随后分叉的子进程的跟踪，但不应影响其他进程。

### 1.2. 实验步骤

- 首先观察`user/trace.c`中的内容，我们需要完成一个`trace`的系统调用，首先在`proc`结构体中我们需要添加一个新的字段`trace_mask`来储存系统调用的参数，以便于`trace`时能打印其相关信息

  ```c
  // kernel/proc.h
  struct proc {
    // ...
    int trace_mask;    // trace系统调用参数
  };
  ```

- 同时，由于`proc`结构体中新增加了该参数，我们还需要在`fork`函数中增加关于该参数的拷贝

- 在`sys_trace`函数中需要通过`argint`函数获取调用参数

  ```c
  // kernel/sysproc.c
  uint64
  sys_trace(void)
  {
    // 获取系统调用的参数
    argint(0, &(myproc()->trace_mask));
    return 0;
  }
  ```

- 最后修改`syscall`函数，当系统调用号匹配时就打印系统调用的相关信息。

  ```c
  //kernel/syscall.c
  void syscall(void) {
    int num;
    struct proc *p = myproc();
  
    num = p->trapframe->a7;
    if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
      p->trapframe->a0 = syscalls[num]();
      if ((1 << num) & p->trace_mask)
        printf("%d: syscall %s -> %d\n", p->pid, syscalls_name[num],
               p->trapframe->a0);
    } else {
      printf("%d %s: unknown sys call %d\n", p->pid, p->name, num);
      p->trapframe->a0 = -1;
    }
  }
  ```

### 1.3 实验中遇到的困难与解决办法

- 不知道如何给系统添加系统调用，查阅相关资料得知，给系统添加系统调用需要：
  1. 在`user/user.h`中添加系统调用的声明
  2. 在`user/usys.pl`中添加入口
  3. 在`kernel/sysycall.h`中添加系统调用号
  4. 在`kernel/syscall.c`中的`syscalls`函数指针数组中添加系统调用所需要的函数

### 1.4 实验心得

通过实现`trace`系统调用，我对于系统调用的工作流程有了一个清晰的认知，使得我对于用户态到内核态的转换过程有了更加清晰的认知，同时也让我学习到了一些c语言中函数指针的灵活用法，可以使用函数指针来实现在不同情况下调用相对应函数的能力。

## 2. Sysinfo (moderate)

### 2.1 实验目的

在本作业中，你将添加一个系统调用 sysinfo，用于收集运行系统的信息。系统调用需要一个参数：指向 struct sysinfo 的指针（参见 kernel/sysinfo.h）。内核应填写该结构体的字段：freemem 字段应设置为可用内存的字节数，nproc 字段应设置为状态不是 UNUSED 的进程数。我们提供了一个测试程序 sysinfotest；如果打印出 "sysinfotest：OK"，则作业通过。

### 2.2 实验步骤

- 同上述`trace`实验，首先完成添加系统调用的准备工作，添加调用函数声明，在函数指针数组中添加`sys_sysinfo`函数指针

- 完成`sys_sysinfo`函数，主要难点就是需要完成两个函数，`freebytes`函数与`procnum`函数，统计内存与进程。

  - `freebytes`函数，遍历`kmem.freelist`，统计空闲内存

    ```c
    // kalloc.c
    void
    freebytes(uint64 *dst)
    {
      *dst = 0;
      struct run *p = kmem.freelist; // 用于遍历
    
      acquire(&kmem.lock);
      while (p) {
        *dst += PGSIZE;
        p = p->next;
      }
      release(&kmem.lock);
    }
    ```

  - 遍历`proc`数组，统计处于活动状态的进程即可

    ```c
    //proc.c
    void
    procnum(uint64 *dst)
    {
      *dst = 0;
      struct proc *p;
      for (p = proc; p < &proc[NPROC]; p++) {
        if (p->state != UNUSED)
          (*dst)++;
      }
    }
    ```

- 实现`sys_sysinfo`

  ```c
  //kernel/sysproc.c
  uint64
  sys_sysinfo(void)
  {
    struct sysinfo info;
    freebytes(&info.freemem);
    procnum(&info.nproc);
  
    // 获取虚拟地址
    uint64 dstaddr;
    argaddr(0, &dstaddr);
  
    // 从内核空间拷贝数据到用户空间
    if (copyout(myproc()->pagetable, dstaddr, (char *)&info, sizeof info) < 0)
      return -1;
  
    return 0;
  }
  ```

### 2.3 实验中遇到的困难与解决方法

- 不清楚如何统计空闲内存与进程数，解决办法是通过查阅阅读`kalloc`和`kfree`两个函数，以及xv6的进程结构体相关源码
- `sysinfo`需要将 `struct sysinfo` 复制到用户空间，通过查阅sys_fstat() (kernel/sysfile.c) 和 filestat() (kernel/file.c)，学习了如何使用`copyout`函数来进行这一步操作。

### 2.4 实验心得

通过实现`sysinfo`系统调用，我对于xv6的内存和进程代码有了一些初步的认识，同时也让我学会了查阅系统源码来获得一些函数操作实例的方法。

