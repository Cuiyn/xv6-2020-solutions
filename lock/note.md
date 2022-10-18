# Locks

本实验将通过重构代码提高并行性。并行性差的一个常见状况是大量的锁争用（lock contention），提高并行性通常需要同时修改数据结构与锁策略。实验将针对 xv6 的内存分配器与块缓存完成上述工作。

## Memory allocator

实验提供了一个测试程序：`user/kalloctest`，此程序对 xv6 的内存分配器进行了一定程度的压力测试，内容是：三个进程增减其地址空间，导致大量的 `kalloc` 与 `kfree` 调用，而这两个调用又需要获取 `kmem.lock`。`kalloctest` 会输出等待获取锁的循环次数，作为对锁争用程度的一个粗略衡量。

实验要求使用一台多核空载机器，因为若机器有其它负载，可能对 `kalloctest` 打印的数据产生影响。

在 `kalloctest` 中，产生锁争用的根本原因是 `kalloc()` 具有单一的 free 列表，并被单一的锁所保护。实验需要重新设计内存分配器，以避免单一的列表与锁。其基本思想是，为每个 CPU 维护一个 free 列表，每个列表有自己的锁。不同 CPU 上的分配和释放可以并行运行，因为每个 CPU 在一个不同的列表上操作。

主要的挑战是如何处理下述情况：一个 CPU 的 free 列表是空的，但另一个 CPU 的列表有空闲内存；在这种情况下，一个 CPU 必须“偷”走另一个 CPU 的 free 列表的一部分。我们希望在这种情况时，不经常发生锁争用。

以下开始修改代码。实验需要修改的代码全部位于 `kernel/kalloc.c` 中。首先，修改结构体声明，将单一的 `kmem` 改为数组：

```c
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];
```

修改 `kinit()` 函数，初始化数组中的每一个锁：

```c
void
kinit()
{
  for(int i = 0; i < NCPU; i ++){
    initlock(&kmem[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}
```

修改 `kfree()` 函数，将释放的内存分配到当前 CPU 对应的 free 列表中：

```c
// cpuid() 需要在中断关闭时调用
push_off();
int cur_cpuid = cpuid();
pop_off();

acquire(&kmem[cur_cpuid].lock);
r->next = kmem[cur_cpuid].freelist;
kmem[cur_cpuid].freelist = r;
release(&kmem[cur_cpuid].lock);
```

修改 `kalloc()` 函数，当前 CPU free 列表为空时，尝试从其它 CPU 的列表中“偷”：

```c
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int cur_cpuid = cpuid();
  pop_off();

  acquire(&kmem[cur_cpuid].lock);
  // 若当前 CPU free 列表为空
  if(!kmem[cur_cpuid].freelist) {
    for (int i = 0; i < NCPU; i ++) {
      // 注意排除当前 CPU，否则 acquire 会出现 panic
      // 其它 CPU free 列表不为空时，“偷取”一个页面
      if(i != cur_cpuid && kmem[i].freelist) {
        acquire(&kmem[i].lock);
        // r_i 为当前 CPU 要“偷取”的页面
        struct run *r_i = kmem[i].freelist;
        // 从其它 CPU 的 free 列表中移除此页面
        kmem[i].freelist = r_i->next;
        // 将新页面添加到当前 CPU 的 free 列表中
        r_i->next = kmem[cur_cpuid].freelist;
        kmem[cur_cpuid].freelist = r_i;
        release(&kmem[i].lock);
      }
    }
  }
  
  // 以下代码不变
  r = kmem[cur_cpuid].freelist;
  if (r)
    kmem[cur_cpuid].freelist = r->next;
  release(&kmem[cur_cpuid].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
```

编译运行 xv6，执行 `kalloctest` 测试，观察到打印数据为 0。执行 `usertests` 全部通过。实验结束。

## Buffer cache

`kernel/bio.c` 提供了保护硬盘块缓存的 `bcache.lock`，当多个进程集中使用文件系统时易引发争用。阅读代码可知，`bcache.lock` 保护了缓存块的缓冲区列表、每个块缓冲区的引用计数（b->refcnt），以及缓存块的身份（b->dev 和 b->blockno）。

实验提供了 `bcachetest` 检测争用。实验要求修改块缓存，使运行 `bcachetest` 时，`bcache` 中所有锁的获取循环迭代次数接近于零。

实验主要涉及 `kernel/bio.c` 中的代码。根据提示，需要实现缓存的散列存储。核心思想是把锁细粒度化，读写缓存时，只对缓存块所在的 bucket 加锁，尽量避免全局加锁。修改代码时，注意加锁后要及时释放，避免 `acquire` 引发错误或死锁。

首先修改 `bcache` 结构体，增加 bucket 与对应的锁，并实现散列函数：

```c
// 实验建议 bucket 数量为 13
#define NBUCKET 13

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  struct buf bucket[NBUCKET];
  struct spinlock bucket_lock[NBUCKET];
} bcache;

inline
int bhash(uint blockno)
{
  return blockno % NBUCKET;
}
```

修改 `binit()` 函数：

```c
void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // 初始化 bucket 锁，并初始化双向链表
  for (int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.bucket_lock[i], "bucket_lock");
    bcache.bucket[i].prev = &bcache.bucket[i];
    bcache.bucket[i].next = &bcache.bucket[i];
  }

  // 由于缓存块初始情况下内容均为 0，直接将所有缓存块放入散列值为 0 的 bucket 中
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.bucket[0].next;
    b->prev = &bcache.bucket[0];
    bcache.bucket[0].next->prev = b;
    bcache.bucket[0].next = b;
  }
  initsleeplock(&b->lock, "buffer");
}
```

修改 `bget()` 函数：

```c
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int hash = bhash(blockno);

  // 删除全局锁
  // 若块已缓存，引用计数加 1，直接返回
  acquire(&bcache.bucket_lock[hash]);
  for(b = bcache.bucket[hash].next; b != &bcache.bucket[hash]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket_lock[hash]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 若块未缓存
  // 判断当前 bucket 中是否有可以替换的块
  for (b = bcache.bucket[hash].next; b != &bcache.bucket[hash]; b = b->next) {
    if (b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.bucket_lock[hash]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 当前 bucket 中没有可以替换的块
  // 从其它 bucket 中“偷”一个
  for (int i = 0; i < NBUCKET; i++) {
    // 排除当前 bucket
    if (i == hash) continue;
    acquire(&bcache.bucket_lock[i]);
    for (b = bcache.bucket[i].next; b != &bcache.bucket[i]; b = b->next) {
      if (b->refcnt == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;

        // 将找到的块加入当前 bucket 中
        b->next->prev = b->prev;
        b->prev->next = b->next;
        b->next = bcache.bucket[hash].next;
        b->prev = &bcache.bucket[hash];
        bcache.bucket[hash].next->prev = b;
        bcache.bucket[hash].next = b;
        release(&bcache.bucket_lock[i]);
        release(&bcache.bucket_lock[hash]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.bucket_lock[i]);
  }
  release(&bcache.bucket_lock[hash]);
  panic("bget: no buffers");
}
```

修改 `brelse()` 函数：

```c
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int hash = bhash(b->blockno);

  // 删除全局锁
  acquire(&bcache.bucket_lock[hash]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.bucket[hash].next;
    b->prev = &bcache.bucket[hash];
    bcache.bucket[hash].next->prev = b;
    bcache.bucket[hash].next = b;
  }

  release(&bcache.bucket_lock[hash]);
}
```

编译运行 xv6，执行 `bcachetest` 测试，观察到打印数据为 0。执行 `usertests` 全部通过。实验结束。
