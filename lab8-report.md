# Locks

## 1. Memory allocator(moderate)

### 1.1 实验目的

程序**user/kalloctest.c**强调了xv6的内存分配器：三个进程增长和缩小地址空间，导致对`kalloc`和`kfree`的多次调用。`kalloc`和`kfree`获得`kmem.lock`。`kalloctest`打印（作为“#fetch-and-add”）在`acquire`中由于尝试获取另一个内核已经持有的锁而进行的循环迭代次数，如`kmem`锁和一些其他锁。`acquire`中的循环迭代次数是锁争用的粗略度量。完成实验前，`kalloctest`的输出与此类似：

```bash
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
lock: kmem: #fetch-and-add 83375 #acquire() 433015
lock: bcache: #fetch-and-add 0 #acquire() 1260
--- top 5 contended locks:
lock: kmem: #fetch-and-add 83375 #acquire() 433015
lock: proc: #fetch-and-add 23737 #acquire() 130718
lock: virtio_disk: #fetch-and-add 11159 #acquire() 114
lock: proc: #fetch-and-add 5937 #acquire() 130786
lock: proc: #fetch-and-add 4080 #acquire() 130786
tot= 83375
test1 FAIL
```

`acquire`为每个锁维护要获取该锁的`acquire`调用计数，以及`acquire`中循环尝试但未能设置锁的次数。`kalloctest`调用一个系统调用，使内核打印`kmem`和`bcache`锁（这是本实验的重点）以及5个最有具竞争的锁的计数。如果存在锁争用，则`acquire`循环迭代的次数将很大。系统调用返回`kmem`和`bcache`锁的循环迭代次数之和。

对于本实验，您必须使用具有多个内核的专用空载机器。如果你使用一台正在做其他事情的机器，`kalloctest`打印的计数将毫无意义。你可以使用专用的Athena 工作站或你自己的笔记本电脑，但不要使用拨号机。

`kalloctest`中锁争用的根本原因是`kalloc()`有一个空闲列表，由一个锁保护。要消除锁争用，您必须重新设计内存分配器，以避免使用单个锁和列表。基本思想是为每个CPU维护一个空闲列表，每个列表都有自己的锁。因为每个CPU将在不同的列表上运行，不同CPU上的分配和释放可以并行运行。主要的挑战将是处理一个CPU的空闲列表为空，而另一个CPU的列表有空闲内存的情况；在这种情况下，一个CPU必须“窃取”另一个CPU空闲列表的一部分。窃取可能会引入锁争用，但这种情况希望不会经常发生。



 **YOUR JOB**

您的工作是实现每个CPU的空闲列表，并在CPU的空闲列表为空时进行窃取。所有锁的命名必须以“`kmem`”开头。也就是说，您应该为每个锁调用`initlock`，并传递一个以“`kmem`”开头的名称。运行`kalloctest`以查看您的实现是否减少了锁争用。要检查它是否仍然可以分配所有内存，请运行`usertests sbrkmuch`。您的输出将与下面所示的类似，在`kmem`锁上的争用总数将大大减少，尽管具体的数字会有所不同。确保`usertests`中的所有测试都通过。评分应该表明考试通过。

```bash
 $ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
lock: kmem: #fetch-and-add 0 #acquire() 42843
lock: kmem: #fetch-and-add 0 #acquire() 198674
lock: kmem: #fetch-and-add 0 #acquire() 191534
lock: bcache: #fetch-and-add 0 #acquire() 1242
--- top 5 contended locks:
lock: proc: #fetch-and-add 43861 #acquire() 117281
lock: virtio_disk: #fetch-and-add 5347 #acquire() 114
lock: proc: #fetch-and-add 4856 #acquire() 117312
lock: proc: #fetch-and-add 4168 #acquire() 117316
lock: proc: #fetch-and-add 2797 #acquire() 117266
tot= 0
test1 OK
start test2
total free number of pages: 32499 (out of 32768)
.....
test2 OK
$ usertests sbrkmuch
usertests starting
test sbrkmuch: OK
ALL TESTS PASSED
$ usertests
...
ALL TESTS PASSED
$
```

### 1.2 实验步骤

改进策略就是为每个CPU核心分配一个空闲链表，`kalloc`和`kfree`都在本核心的链表上进行，只有当当前核心的链表为空时才去访问其他核心的链表。通过这种策略就可以减少锁的争用，只有当某核心的链表为空时才会发生锁争用。

首先定义NCPU个`kmem`结构体，并在`kinit`函数中对锁进行初始化。

```c
struct {
  struct spinlock lock;
  struct run *freelist;
  char lock_name[7];
} kmem[NCPU];

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    snprintf(kmem[i].lock_name, sizeof(kmem[i].lock_name), "kmem_%d", i);
    initlock(&kmem[i].lock, kmem[i].lock_name);
  }
  freerange(end, (void*)PHYSTOP);
}
```

对于`kfree`函数只需要将释放的页面插入到当前核心对应链表上就行了

```c
void
kfree(void *pa)
{
  ...
  r = (struct run*)pa;

  push_off();
  int id = cpuid();

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);

  pop_off();
}
```

对于`kalloc`函数，当在当前核心上申请失败时，就尝试从其他核心上获取页面。使用快慢指针来找到链表的中点，之后将一半的页面移动到当前核心的链表上。

```c
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int id = cpuid();

  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r) {
    kmem[id].freelist = r->next;
  }
  else {
    // alloc failed, try to steal from other cpu
    int success = 0;
    int i = 0;
    for(i = 0; i < NCPU; i++) {
      if (i == id) continue;
      acquire(&kmem[i].lock);
      struct run *p = kmem[i].freelist;
      if(p) {
        // steal half of memory
        struct run *fp = p; // faster pointer
        struct run *pre = p;
        while (fp && fp->next) {
          fp = fp->next->next;
          pre = p;
          p = p->next;
        }
        kmem[id].freelist = kmem[i].freelist;
        if (p == kmem[i].freelist) {
          // only have one page
          kmem[i].freelist = 0;
        }
        else {
          kmem[i].freelist = p;
          pre->next = 0;
        }
        success = 1;
      }
      release(&kmem[i].lock);
      if (success) {
        r = kmem[id].freelist;
        kmem[id].freelist = r->next;
        break;
      }
    }
  }
  release(&kmem[id].lock);
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
```

### 1.3 实验结果

实验结果如下：

```bash
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
lock: kmem_0: #fetch-and-add 0 #acquire() 77186
lock: kmem_1: #fetch-and-add 0 #acquire() 182362
lock: kmem_2: #fetch-and-add 0 #acquire() 173534
lock: bcache_bucket: #fetch-and-add 0 #acquire() 128
lock: bcache_bucket: #fetch-and-add 0 #acquire() 138
lock: bcache_bucket: #fetch-and-add 0 #acquire() 142
lock: bcache_bucket: #fetch-and-add 0 #acquire() 148
lock: bcache_bucket: #fetch-and-add 0 #acquire() 132
lock: bcache_bucket: #fetch-and-add 0 #acquire() 6
lock: bcache_bucket: #fetch-and-add 0 #acquire() 42
lock: bcache_bucket: #fetch-and-add 0 #acquire() 34
lock: bcache_bucket: #fetch-and-add 0 #acquire() 5916
lock: bcache_bucket: #fetch-and-add 0 #acquire() 32
lock: bcache_bucket: #fetch-and-add 0 #acquire() 242
lock: bcache_bucket: #fetch-and-add 0 #acquire() 128
lock: bcache_bucket: #fetch-and-add 0 #acquire() 128
--- top 5 contended locks:
lock: proc: #fetch-and-add 31954 #acquire() 206502
lock: proc: #fetch-and-add 24395 #acquire() 206518
lock: proc: #fetch-and-add 9306 #acquire() 206501
lock: proc: #fetch-and-add 7463 #acquire() 206481
lock: proc: #fetch-and-add 5209 #acquire() 206480
tot= 0
test1 OK
start test2
total free number of pages: 32493 (out of 32768)
.....
test2 OK
```
## 2. Buffer cache (hard)

### 2.1 实验目的

这一半作业独立于前一半；不管你是否完成了前半部分，你都可以完成这半部分（并通过测试）。

如果多个进程密集地使用文件系统，它们可能会争夺`bcache.lock`，它保护**kernel/bio.c**中的磁盘块缓存。`bcachetest`创建多个进程，这些进程重复读取不同的文件，以便在`bcache.lock`上生成争用；（在完成本实验之前）其输出如下所示：

```bash
$ bcachetest
start test0
test0 results:
--- lock kmem/bcache stats
lock: kmem: #fetch-and-add 0 #acquire() 33035
lock: bcache: #fetch-and-add 16142 #acquire() 65978
--- top 5 contended locks:
lock: virtio_disk: #fetch-and-add 162870 #acquire() 1188
lock: proc: #fetch-and-add 51936 #acquire() 73732
lock: bcache: #fetch-and-add 16142 #acquire() 65978
lock: uart: #fetch-and-add 7505 #acquire() 117
lock: proc: #fetch-and-add 6937 #acquire() 73420
tot= 16142
test0: FAIL
start test1
test1 OK
```

您可能会看到不同的输出，但`bcache`锁的`acquire`循环迭代次数将很高。如果查看**kernel/bio.c**中的代码，您将看到`bcache.lock`保护已缓存的块缓冲区的列表、每个块缓冲区中的引用计数（`b->refcnt`）以及缓存块的标识（`b->dev`和`b->blockno`）。

 **YOUR JOB**

修改块缓存，以便在运行`bcachetest`时，bcache（buffer cache的缩写）中所有锁的`acquire`循环迭代次数接近于零。理想情况下，块缓存中涉及的所有锁的计数总和应为零，但只要总和小于500就可以。修改`bget`和`brelse`，以便bcache中不同块的并发查找和释放不太可能在锁上发生冲突（例如，不必全部等待`bcache.lock`）。你必须保护每个块最多缓存一个副本的不变量。完成后，您的输出应该与下面显示的类似（尽管不完全相同）。确保`usertests`仍然通过。完成后，`make grade`应该通过所有测试。

```bash
$ bcachetest
start test0
test0 results:
--- lock kmem/bcache stats
lock: kmem: #fetch-and-add 0 #acquire() 32954
lock: kmem: #fetch-and-add 0 #acquire() 75
lock: kmem: #fetch-and-add 0 #acquire() 73
lock: bcache: #fetch-and-add 0 #acquire() 85
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4159
lock: bcache.bucket: #fetch-and-add 0 #acquire() 2118
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4274
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4326
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6334
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6321
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6704
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6696
lock: bcache.bucket: #fetch-and-add 0 #acquire() 7757
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6199
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4136
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4136
lock: bcache.bucket: #fetch-and-add 0 #acquire() 2123
--- top 5 contended locks:
lock: virtio_disk: #fetch-and-add 158235 #acquire() 1193
lock: proc: #fetch-and-add 117563 #acquire() 3708493
lock: proc: #fetch-and-add 65921 #acquire() 3710254
lock: proc: #fetch-and-add 44090 #acquire() 3708607
lock: proc: #fetch-and-add 43252 #acquire() 3708521
tot= 128
test0: OK
start test1
test1 OK
$ usertests
  ...
ALL TESTS PASSED
$
```

请将你所有的锁以“`bcache`”开头进行命名。也就是说，您应该为每个锁调用`initlock`，并传递一个以“`bcache`”开头的名称。

减少块缓存中的争用比`kalloc`更复杂，因为bcache缓冲区真正的在进程（以及CPU）之间共享。对于`kalloc`，可以通过给每个CPU设置自己的分配器来消除大部分争用；这对块缓存不起作用。我们建议您使用每个哈希桶都有一个锁的哈希表在缓存中查找块号。

在您的解决方案中，以下是一些存在锁冲突但可以接受的情形：

- 当两个进程同时使用相同的块号时。`bcachetest test0`始终不会这样做。
- 当两个进程同时在cache中未命中时，需要找到一个未使用的块进行替换。`bcachetest test0`始终不会这样做。
- 在你用来划分块和锁的方案中某些块可能会发生冲突，当两个进程同时使用冲突的块时。例如，如果两个进程使用的块，其块号散列到哈希表中相同的槽。`bcachetest test0`可能会执行此操作，具体取决于您的设计，但您应该尝试调整方案的细节以避免冲突（例如，更改哈希表的大小）。

`bcachetest`的`test1`使用的块比缓冲区更多，并且执行大量文件系统代码路径。

### 2.2 实验步骤

为了提高并行性能，我们可以用哈希表来代替链表，这样每次获取和释放的时候，都只需要对哈希表的一个桶进行加锁，桶之间的操作就可以并行进行。只有当需要对缓冲区进行驱逐替换时，才需要对整个哈希表加锁来查找要替换的块。

使用哈希表就不能使用链表来维护LRU信息，因此需要在`buf`结构体中添加`timestamp`域来记录释放的事件，同时`prev`域也不再需要。

```c
struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  // struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];

  uint timestamp;
};
```

在`brelse`函数中对`timestamp`域进行维护，同时将链表的锁替换为桶级锁：

```c
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int idx = hash(b->dev, b->blockno);

  acquire(&hashtable[idx].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->timestamp = ticks;
  }
  
  release(&hashtable[idx].lock);
}
```

定义哈希表的结构体，`bcache.lock`为表级锁，而`hashtable[i].lock`为桶级锁：

```c
#define NBUCKET 13
#define NBUF (NBUCKET * 3)

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
} bcache;

struct bucket {
  struct spinlock lock;
  struct buf head;
}hashtable[NBUCKET];

int
hash(uint dev, uint blockno)
{
  return blockno % NBUCKET;
}
```

在`binit`函数中对哈希表进行初始化，将`bcache.buf[NBUF]`中的块平均分配给每个桶，记得设置`b->blockno`使块的hash与桶相对应，后面需要根据块来查找对应的桶。

```c
void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
  }

  b = bcache.buf;
  for (int i = 0; i < NBUCKET; i++) {
    initlock(&hashtable[i].lock, "bcache_bucket");
    for (int j = 0; j < NBUF / NBUCKET; j++) {
      b->blockno = i; // hash(b) should equal to i
      b->next = hashtable[i].head.next;
      hashtable[i].head.next = b;
      b++;
    }
  }
}
```

之后就是重点`bget`函数，首先在对应的桶当中查找当前块是否被缓存，如果被缓存就直接返回；如果没有被缓存的话，就需要查找一个块并将其逐出替换。我这里使用的策略是先在当前桶当中查找，当前桶没有查找到再去全局数组中查找，这样的话如果当前桶中有空闲块就可以避免全局锁。

在全局数组中查找时，要先加上表级锁，当找到一个块之后，就可以根据块的信息查找到对应的桶，之后再对该桶加锁，将块从桶的链表上取下来，释放锁，最后再加到当前桶的链表上去。

这里有个小问题就是全局数组中找到一个块之后，到对该桶加上锁之间有一个窗口，可能就在这个窗口里面这个块就被那个桶对应的本地查找阶段用掉了。因此，需要在加上锁之后判断是否被用了，如果被用了就要重新查找。

```c
static struct buf*
bget(uint dev, uint blockno)
{
  // printf("dev: %d blockno: %d Status: ", dev, blockno);
  struct buf *b;

  int idx = hash(dev, blockno);
  struct bucket* bucket = hashtable + idx;
  acquire(&bucket->lock);

  // Is the block already cached?
  for(b = bucket->head.next; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bucket->lock);
      acquiresleep(&b->lock);
      // printf("Cached %p\n", b);
      return b;
    }
  }

  // Not cached.
  // First try to find in current bucket.
  int min_time = 0x8fffffff;
  struct buf* replace_buf = 0;

  for(b = bucket->head.next; b != 0; b = b->next){
    if(b->refcnt == 0 && b->timestamp < min_time) {
      replace_buf = b;
      min_time = b->timestamp;
    }
  }
  if(replace_buf) {
    // printf("Local %d %p\n", idx, replace_buf);
    goto find;
  }

  // Try to find in other bucket.
  acquire(&bcache.lock);
  refind:
  for(b = bcache.buf; b < bcache.buf + NBUF; b++) {
    if(b->refcnt == 0 && b->timestamp < min_time) {
      replace_buf = b;
      min_time = b->timestamp;
    }
  }
  if (replace_buf) {
    // remove from old bucket
    int ridx = hash(replace_buf->dev, replace_buf->blockno);
    acquire(&hashtable[ridx].lock);
    if(replace_buf->refcnt != 0)  // be used in another bucket's local find between finded and acquire
    {
      release(&hashtable[ridx].lock);
      goto refind;
    }
    struct buf *pre = &hashtable[ridx].head;
    struct buf *p = hashtable[ridx].head.next;
    while (p != replace_buf) {
      pre = pre->next;
      p = p->next;
    }
    pre->next = p->next;
    release(&hashtable[ridx].lock);
    // add to current bucket
    replace_buf->next = hashtable[idx].head.next;
    hashtable[idx].head.next = replace_buf;
    release(&bcache.lock);
    // printf("Global %d -> %d %p\n", ridx, idx, replace_buf);
    goto find;
  }
  else {
    panic("bget: no buffers");
  }

  find:
  replace_buf->dev = dev;
  replace_buf->blockno = blockno;
  replace_buf->valid = 0;
  replace_buf->refcnt = 1;
  release(&bucket->lock);
  acquiresleep(&replace_buf->lock);
  return replace_buf;
}
```

最后将`bpin`和`bunpin`的锁替换为桶级锁就行了：

```c
void
bpin(struct buf *b) {
  int idx = hash(b->dev, b->blockno);
  acquire(&hashtable[idx].lock);
  b->refcnt++;
  release(&hashtable[idx].lock);
}

void
bunpin(struct buf *b) {
  int idx = hash(b->dev, b->blockno);
  acquire(&hashtable[idx].lock);
  b->refcnt--;
  release(&hashtable[idx].lock);
}
```

### 2.3 实验结果

```bash
start test0
test0 results:
--- lock kmem/bcache stats
...
lock: bcache_bucket: #fetch-and-add 0 #acquire() 4244
lock: bcache_bucket: #fetch-and-add 0 #acquire() 2246
lock: bcache_bucket: #fetch-and-add 0 #acquire() 4402
lock: bcache_bucket: #fetch-and-add 0 #acquire() 4458
lock: bcache_bucket: #fetch-and-add 0 #acquire() 6450
lock: bcache_bucket: #fetch-and-add 0 #acquire() 6312
lock: bcache_bucket: #fetch-and-add 0 #acquire() 6624
lock: bcache_bucket: #fetch-and-add 0 #acquire() 6634
lock: bcache_bucket: #fetch-and-add 0 #acquire() 12706
lock: bcache_bucket: #fetch-and-add 0 #acquire() 6208
lock: bcache_bucket: #fetch-and-add 0 #acquire() 4360
lock: bcache_bucket: #fetch-and-add 0 #acquire() 4246
lock: bcache_bucket: #fetch-and-add 0 #acquire() 2236
--- top 5 contended locks:
lock: proc: #fetch-and-add 269741 #acquire() 4551132
lock: proc: #fetch-and-add 236112 #acquire() 4551131
lock: proc: #fetch-and-add 186278 #acquire() 4551151
lock: proc: #fetch-and-add 167286 #acquire() 4551164
lock: proc: #fetch-and-add 151922 #acquire() 4551132
tot= 0
test0: OK
start test1
test1 OK
```

## 3. 实验心得
当我们在进行多线程编程时，锁的使用是非常重要的。在本次实验中，我们使用了 bcache_bucket 和 proc 两种锁，通过分析锁的争用情况，我们可以发现哪些锁是瓶颈，从而进行优化。同时，我们还学习了如何使用 perf 工具来进行性能分析，以及如何使用 ftrace 工具来进行函数调用跟踪。这些工具可以帮助我们更好地理解程序的性能瓶颈，并进行优化。

总的来说，本次实验让我更深入地了解了多线程编程和性能分析的相关知识，也让我对操作系统的工作原理有了更深入的理解。
