# Copy-on-Write Fork for xv6

虚拟内存提供了一种间接性：操作系统内核能够通过将 PTE 标记为无效或只读，以拦截内存引用，导致 page fault，并可以通过修改 PTE 来改变地址的含义。

在计算机系统中，有一种说法：任何系统问题都可以通过一定程度的间接性来解决。上一实验的懒分配就是一个例子。本实验探讨了另一个例子：写时复制分叉。

写时复制（COW）`fork()` 的目标是推迟为子进程分配和复制物理内存页，直到子进程实际需要这些页的时候。

COW `fork()` 仅为子进程创建一个页表，其中用户内存的 PTE 指向父进程的物理页。COW `fork()` 标记父子进程的所有用户 PTE 为不可写。当任何一个进程试图写入这些 COW 页面时，CPU 将强制发生 page fault。内核处理程序检测到此种情况时，为引发 page fault 的进程分配一个物理内存页，将原页复制到新页中，并修改进程中的相关PTE以引用新的页，此时PTE被标记为可写。当处理程序返回时，进程将能够写入页副本。

COW `fork()` 使得释放用户内存的物理页变得困难。一个给定的物理页可能被多个进程的页表所引用，只有当最后一个引用消失时才应该被释放。

## Implement copy-on write

首先，需要定义 PTE COW 标志位。查 Sv39 PTE 定义可知，第 8-9 两位为预留，我们取第 8 位作为 COW 标志。在 `kernel/riscv.h` 中添加：

```c
#define PTE_COW (1L << 8)
```

此位为 1 时，代表当前页为 COW 页。

实现其它功能之前，需要先实现 COW 页面的引用计数。`kernel/kalloc.c` 中实现了负责物理内存管理的一系列函数。首先添加引用计数数据结构：

```c
struct {
  // 防止并行出错
  struct spinlock lock;
  uint counter[(PHYSTOP - KERNBASE) / PGSIZE];
} cowcnt;

// 添加辅助函数，以便在其它代码文件中使用
// 注意，新增函数需要在 kernel/defs.h 中声明，以下不再提示
inline void lockcowcnt() { return acquire(&cowcnt.lock); }

inline void unlockcowcnt() { return release(&cowcnt.lock); }

inline void setcowcnt(uint64 pa, uint64 n) {
  cowcnt.counter[(pa - KERNBASE) / PGSIZE] = n;
}

inline uint64 getcowcnt(uint64 pa) {
  return cowcnt.counter[(pa - KERNBASE) / PGSIZE];
}

```

修改 `kfree()` 函数，进行物理内存释放前，首先判断引用计数是否大于 1，否则只修改引用计数，不释放内存。

```c
void
kfree(void *pa)
{
  struct run *r;

  // 若引用计数值为 1，则不释放内存，仅将引用计数减 1
  lockcowcnt();
  if (getcowcnt((uint64)pa) > 1){
    setcowcnt((uint64)pa, getcowcnt((uint64)pa) - 1);
    unlockcowcnt();
    return;
  }

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  memset(pa, 1, PGSIZE);

  // 对要释放的内存，初始化其引用计数
  setcowcnt((uint64)pa, 0);
  unlockcowcnt();

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}
```

修改 `kalloc()` 函数，将成功分配内存的引用计数设置为 1。

```c
void *
kalloc(void)
{
  // 省略
  if(r){
    acquire(&cowcnt.lock);
    cowcnt.counter[((uint64)r - KERNBASE) / PGSIZE] += 1;
    release(&cowcnt.lock);
  }
  return (void*)r;
}
```

此时，满足了实现 COW 的先决条件──实现引用计数。

修改 `kernel/vm.c` 中的 `uvmcopy()` 函数，复制父进程内存时，不立即复制数据，只建立指向物理内存的映射，并设置父子进程的 PTE 为不可写、COW 页。

```c
for(i = 0; i < sz; i += PGSIZE){
  if((pte = walk(old, i, 0)) == 0)
    panic("uvmcopy: pte should exist");
  if((*pte & PTE_V) == 0)
    panic("uvmcopy: page not present");
  pa = PTE2PA(*pte);
  // PTE_W 置 0，PTE_COW 置 1
  flags = PTE_FLAGS(*pte); 
  *pte = (*pte & ~PTE_W) | PTE_COW;
  // 删除分配内存、复制内容的相关代码
  if(mappages(new, i, PGSIZE, (uint64)pa, flags) != 0){
    goto err;
  }
}
// 由于对父进程内存进行了 COW，其引用计数应增 1
uint64 cnt = getcowcnt(pa);
setcowcnt(pa, cnt + 1);
```

此时编译运行 xv6，运行任意命令，系统报错，scause 值为 `0xf`、`0x2` 与 `0xc`。查 scause 错误码表可知，13 对应读页面错误，15 对应写页面错误。由于 COW 只会引发写错误，因此只需在 `kernel/trap.c` 中的 `usertrap()` 中添加对错误 15 的处理：

```c
if(r_scause() == 15){
  uint64 va = r_stval();
  // 复制 COW 页面
  // 失败时应结束进程
  if(cowcpy(va) != 0)
    p->killed = 1;
}
```

此外，与上一实验类似，`kernel/vm.c` 中的 `copyout()` 由于是软件访问页表，不会触发缺页异常，因此同样需要判断目的页面是否为 COW 页面，若是则需要复制：

```c
while(len > 0) {
  va0 = PGROUNDDOWN(dstva);

  pte_t *pte = walk(pagetable, va0, 0);
  if (pte && (*pte & PTE_COW) != 0) {
    // 目的页面是 COW 页面，进行复制
    if (cowcpy(va0) != 0) {
      return -1;
    }
  }
  // 省略
}
```

上述 `cowcpy()` 函数在 `kernel/vm.c` 中实现：

```c
int
cowcpy(uint64 va) {
  va = PGROUNDDOWN(va);
  pte_t *pte;
  pagetable_t p = myproc()->pagetable;

  if ((pte = walk(p, va, 0)) == 0)
    panic("cowcpy: walk");

  uint64 pa = PTE2PA(*pte);
  uint flags = PTE_FLAGS(*pte);
  if (!(flags & PTE_COW))
    panic("cowcpy: not cow");

  lockcowcnt();
  uint64 cnt = getcowcnt(pa);

  if (cnt == 1) {
    // 若引用计数值为 1，直接使用原页面即可
    // 去除 COW 标志位
    *pte = ((*pte) & (~PTE_COW)) | PTE_W;
    unlockcowcnt();
    return 0;
  } else {
    // 若引用计数值大于 1，则需要申请一个新页面并复制内容
    
    // 由于之前已经对引用计数结构加锁，这里不应再加
    // kalloc_cow() 为 kalloc() 去掉对引用计数加锁的版本
    uint64 *newmem = kalloc_cow();
    if (newmem == 0) {
      unlockcowcnt();
      return -1;
    }

    memmove(newmem, (void *)pa, PGSIZE);
    // 建立映射时，由于新页面非 COW，应去除 COW 标志位
    if (mappages(p, va, PGSIZE, (uint64)newmem, (flags & (~PTE_COW)) | PTE_W) != 0) {
      kfree(newmem);
      unlockcowcnt();
      return -1;
    }

    // 原页面引用计数减 1
    setcowcnt(pa, cnt - 1);
    unlockcowcnt();
    return 0;
  }
}
```

运行 xv6，观察到 `mappages()` 函数可能会抛出 `remap` 错误，这是因为 COW 确实进行了 remap，去掉报错代码即可。

再次运行 xv6，执行 `cowtest` 与 `usertests` 两个测试程序，观察到所有测试均通过。实验结束。

## 参考内容

实验参考了以下两篇文章的解决方法，谨致感谢：

<https://juejin.cn/post/7015477978666631182>

<https://www.cnblogs.com/weijunji/p/xv6-study-9.html>
