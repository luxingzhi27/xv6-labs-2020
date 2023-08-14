# traps

## 1. RISC-V assembly (easy)

### 1.1 实验目的

- 理解RISC-V的汇编语言

### 1.2 实验步骤

1. 哪些寄存器保存函数的参数？例如，在`main`对`printf`的调用中，哪个寄存器保存13？

    RISC-V的函数调用过程参数优先使用寄存器传递，即a0~a7共8个寄存器。返回值可以放在a0和a1寄存器。printf的参数13保存在a2寄存器。

2. `main`的汇编代码中对函数`f`的调用在哪里？对`g`的调用在哪里(提示：编译器可能会将函数内联）

    从代码可以看出，这两个都被内联优化处理了。main中的f调用直接使用了结果12，而f中的函数g调用直接内联在f中了。

3. `printf`函数位于哪个地址？

    0x630

4. 在`main`中`printf`的`jalr`之后的寄存器`ra`中有什么值？

    值为0x38，即函数的返回地址。

5. 运行以下代码。

    ```c
    unsigned int i = 0x00646c72;
    printf("H%x Wo%s", 57616, &i);
    ```

    程序的输出是什么？这是将字节映射到字符的[ASCII码表](http://web.cs.mun.ca/~michael/c/ascii-table.html)。

    输出取决于RISC-V小端存储的事实。如果RISC-V是大端存储，为了得到相同的输出，你会把`i`设置成什么？是否需要将`57616`更改为其他值？

    [这里有一个小端和大端存储的描述](http://www.webopedia.com/TERM/b/big_endian.html)和一个[更异想天开的描述](http://www.networksorcery.com/enp/ien/ien137.txt)。
    
    57616=0xE110，0x00646c72小端存储为72-6c-64-00，对照ASCII码表
    
    72:r 6c:l 64:d 00:充当字符串结尾标识
    
    因此输出为：HE110 World
    
    若为大端存储，i应改为0x726c6400，不需改变57616
    
  6. 在下面的代码中，“`y=`”之后将打印什么(注：答案不是一个特定的值）？为什么会发生这种情况？

    ```c
    printf("x=%d y=%d", 3);
    ```
    
    原本需要两个参数，却只传入了一个，因此y=后面打印的结果取决于之前a2中保存的数据

### 1.3 实验心得

大致为简单了解了risc-v的汇编语言，能看懂一些简单汇编代码

## 2. Backtrace (moderate)

### 2.1 实验目的

回溯(Backtrace)通常对于调试很有用：它是一个存放于栈上用于指示错误发生位置的函数调用列表。

在**kernel/printf.c**中实现名为**backtrace()**的函数。在**sys_sleep**中插入一个对此函数的调用，然后运行**bttest**，它将会调用**sys_sleep**。你的输出应该如下所示：
```shell
backtrace:
0x0000000080002cda
0x0000000080002bb6
0x0000000080002898
```
 在bttest退出qemu后。在你的终端：地址或许会稍有不同，但如果你运行addr2line -e kernel/kernel（或riscv64-unknown-elf-addr2line -e kernel/kernel），并将上面的地址剪切粘贴如下：

```shell
$ addr2line -e kernel/kernel
0x0000000080002de2
0x0000000080002f4a
0x0000000080002bfc
Ctrl-D
```
你应该看到类似下面的输出：
```shell
kernel/sysproc.c:74
kernel/syscall.c:224
kernel/trap.c:85
```
 编译器向每一个栈帧中放置一个帧指针（frame pointer）保存调用者帧指针的地址。你的backtrace应当使用这些帧指针来遍历栈，并在每个栈帧中打印保存的返回地址。

一旦你的`backtrace`能够运行，就在**kernel/printf.c**的`panic`中调用它，那样你就可以在`panic`发生时看到内核的`backtrace`。

### 2.2 实验步骤

实现**backtrace**，递归打印函数调用栈。使用r_fp获取当前栈帧地址，由于栈是由高地址向低地址增长的，因此使用**PGROUNDUP**获得栈底地址，之后循环打印栈帧的函数的返回地址。

```c
void
backtrace(void)
{
  printf("backtrace:\n");
  uint64 fp = r_fp();
  uint64 base = PGROUNDUP(fp);
  while(fp < base) {
    printf("%p\n", *((uint64*)(fp - 8)));
    fp = *((uint64*)(fp - 16));
  }
}
```

### 2.3 实验心得

此函数就是实现曾经调用函数地址的回溯，编译器报错时就是类似的逻辑，通过编写`backtrace`函数，可以方便我们调试代码

## 3. Alarm(hard)

### 3.1 实验目的

在这个练习中你将向XV6添加一个特性，在进程使用CPU的时间内，XV6定期向进程发出警报。这对于那些希望限制CPU时间消耗的受计算限制的进程，或者对于那些计算的同时执行某些周期性操作的进程可能很有用。更普遍的来说，你将实现用户级中断/故障处理程序的一种初级形式。例如，你可以在应用程序中使用类似的一些东西处理页面故障。如果你的解决方案通过了`alarmtest`和`usertests`就是正确的。

### 3.2 实验步骤

程序计数器的过程是这样的：

1. `ecall`指令中将PC保存到SEPC
2. 在`usertrap`中将SEPC保存到`p->trapframe->epc`
3. `p->trapframe->epc`加4指向下一条指令
4. 执行系统调用
5. 在`usertrapret`中将SEPC改写为`p->trapframe->epc`中的值
6. 在`sret`中将PC设置为SEPC的值

执行系统调用后返回到用户空间继续执行的指令地址是由`p->trapframe->epc`决定的，因此在`usertrap`中主要就是完成它的设置工作。

1. 首先在`proc`结构体中添加相应字段：

   ```c
   struct proc {
     ...
     // these are used for sys_alarm
     int duration;                // ticks after last alarm
     int alarm;                   // alarm every n ticks
     uint64 handler;              // handler for alarm
     struct trapframe *alarm_trapframe; // register saved for alarm
   };
   ```

2. 实现`sys_alarm`函数，将相关信息填入`proc`中

   ```c
   uint64
   sys_sigalarm(void)
   {
     int ticks;
     uint64 handler;
     if(argint(0, &ticks) < 0)
       return -1;
     if(argaddr(1, &handler) < 0)
       return -1;
     
     struct proc* p = myproc();
     p->alarm = ticks;
     p->handler = handler;
     p->duration = 0;
     p->alarm_trapframe = 0;
     return 0;
   }
   ```

3. 当发生时钟中断时，将`p->duration`增加，如果`p->duration == p->alarm`，那么就要触发一次回调函数，而触发的方法就是将`p->trapframe->epc`设置为回调函数地址，当陷阱处理程序结束后就会跳转到回调函数。

   而为了保证回调函数不会破坏原程序的寄存器，需要对`trapframe`进行保存；我这里选择的方法是通过`kalloc`申请一个新的`trapframe`结构体，然后将`trapframe`复制一份。

   为了保证回调函数执行期间不会重复调用，就可以判断`p->alarm_trapframe`是否为0，不为0说明上一次的回调函数还没有调用`sigreturn`，即函数未结束。

   ```c
     if(which_dev == 2){
       if(p->alarm != 0){
         p->duration++;
         if(p->duration == p->alarm){
           p->duration = 0;
           if(p->alarm_trapframe == 0){
             p->alarm_trapframe = kalloc();
             memmove(p->alarm_trapframe, p->trapframe, 512);
             p->trapframe->epc = p->handler;
           }else{
             yield();
           }
         }else{
           yield();
         }
       }else{
         yield();
       }
     }
   ```

4. `sigreturn`函数，这个函数要做的工作就是将之前保存的`alarm_trapframe`还原到`trapframe`中，并将`alarm_trapframe`释放掉。别忘了在`freeproc`函数中也要对`p->alarm_trapframe`进行判断，防止程序异常结束时该页面没有被释放。

   ```c
   uint64
   sys_sigreturn(void)
   {
     struct proc* p = myproc();
     if(p->alarm_trapframe != 0){
       memmove(p->trapframe, p->alarm_trapframe, 512);
       kfree(p->alarm_trapframe);
       p->alarm_trapframe = 0;
     }
     return 0;
   }
   ```

### 3.3 实验心得

在本次实验中，我学习了如何在xv6操作系统中实现**alarm**功能。通过修改操作系统内核代码，我成功地实现了**alarm**功能，并在用户程序中进行了测试。本次实验让我对操作系统的中断机制和系统调用有了更深入的了解，也提高了我的编程能力和解决问题的能力。

