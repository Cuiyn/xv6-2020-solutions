# Lazy Page Allocation

本 lab 针对用户内存空间的**懒分配**设计。运行在 xv6 程序可以通过调用 `sbrk()` 系统调用，请求内核分配堆内存空间。在实验给定的 xv6 版本中，`sbrk()` 立即分配物理内存，并映射到进行的虚拟地址空间，而这一方式易导致时间与资源的浪费。实验要求为 xv6 添加懒分配特性，当进程调用 `sbrk()` 时，内核仅记录哪些用户地址被分配，并标记这些地址在用户页表中是无效的。当进程第一次试图访问使用懒加载分配的内存时，CPU 生成 page fault，内核通过分配物理内存、清零内存、映射来处理它。

## Eliminate allocation from sbrk()

本实验要求删除 `sbrk()` 系统调用中关于页分配的实现。该系统调用的代码位于 `kernel/sysproc.c` 中。新的 `sbrk()` 代码须仅增加进程的内存大小并返回旧的大小，而不进行分配。

根据实验说明，对 `sys_sbrk()`  代码进行调整。

```c
uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;

  // 取消分配
  // if(growproc(n) < 0)
  // return -1;

  // 增加大小
  myproc()->sz += n;
  return addr;
}
```

编译运行 xv6，运行 `echo hi` 命令，观查输出为：

```shell
$ echo hi
usertrap(): unexpected scause 0x000000000000000f pid=3
            sepc=0x00000000000012d4 stval=0x0000000000004008
panic: uvmunmap: not mapped
```

`usertrap(): ...` 信息由 `trap.c` 中的 trap handler 输出，解释为：
- `scause` 寄存器保存了 trap 进入到 supervisor mode 的原因，15 表示是因为 store 指令引起的 page fault。
- `pid` 即为引起 trap 的进程 id。这里 3 极有可能是 shell 的 pid。
- `sepc`（Supervisor Exception Program Counter）寄存器保存了触发 page fault 的指令的地址。
- `stval` 寄存器保存了出错的虚拟内存地址。

## Lazy allocation

本实验要求修改 `kernel/trap.c` 中的代码，通过在 fault 地址映射一个新分配的物理内存页来响应来自用户空间的 page fault，之后返回到用户空间，让进程继续执行。实验要求在输出 `usertrap(): ...` 的 `printf` 调用之前添加代码，并修改其它 xv6 内核代码以使得 `echo hi` 能够正常工作。

根据上述说明，修改 `usertrap()` 函数代码，在错误信息代码输出前添加：

```c
// 省略
} else if(r_scause() == 15){
  uint64 va = r_stval();
  printf("page fault: %p\n", va);
  uint64 ka = (uint64) kalloc();
  if (ka == 0) {
    p->killed = 1;
  } else {
    memset((void *) ka, 0, PGSIZE);
    va = PGROUNDDOWN(va);
    if (mappages(p->pagetable, va, PGSIZE, ka, PTE_W|PTE_U|PTE_R) != 0) {
      kfree((void *)ka);
      p->killed = 1;
    }
  }
} else {
// 省略
```

首先输出了产生 page fault 的虚拟内存地址，之后尝试分配物理内存空间。若有物理内存，再映射至用户地址空间中合适的虚拟内存地址。具体而言，首先将虚拟地址向下取整，再将物理内存地址与取整之后的虚拟内存地址的关系加到 page table 中。

此时运行 xv6，执行 `echo hi`，输出结果为：

```shell
$ echo hi
page fault: 0x0000000000004008
page fault: 0x0000000000013f48
panic: uvmunmap: not mapped
```

观察到 `uvmunmap` 在报错，相关代码在 `kernel/vm.c` 中。原因是，xv6 试图 unmap 的地址是懒分配的地址，尚未实际分配，自然不可以 unmap。对于之前未修改的 xv6，永远也不会出现用户内存未映射的情况，所以一旦出现这种情况需要产生 panic。但是现在 xv6 已被修改，所以需要去掉这里的 panic，因为之前的不可能变成了可能。将 panic 语句换为 `continue` 即可。

再次编译运行 xv6，执行 `echo hi`，输出结果为：

```shell
$ echo hi
page fault: 0x0000000000004008
page fault: 0x0000000000013f48
hi
```

此时 `echo hi` 能够正常工作。

## Lazytests and Usertests

本实验提供了两个关于懒分配的测试程序：`lazytests` 与 `usertests`，要求修改内核代码以通过上述两个测试。实验给出的要求有：
- `sbrk()` 能够处理负数（即减少内存）。
- 若 page fault 地址比 `sbrk()` 分配的地址还要高，则杀掉进程。
- 正确处理 `fork()` 中从父进程到子进程的内存复制。
- 处理下述情况：进程向系统调用（如读或写）传递由 `sbrk()` 获得的有效地址，但该地址的内存尚未被分配。
- 正确处理 out-of-memory，如果 `kalloc()` 在 page fault 处理中失败，则杀掉进程。
- 处理用户堆栈下的无效页面故障。

首先，为后续方便，将上一实验对 `usertrap()` 函数代码的修改拆分为函数，放在 `kernel/vm.c` 文件中。由于使用了 `proc` 结构体，注意引入相应头文件，并在 `defs.h` 中添加函数定义：

```c
void uvmlazyallocate(uint64 va) {
  struct proc *p = myproc();
  uint64 ka = (uint64) kalloc();
  if (ka == 0) {
    p->killed = 1;
  } else {
    memset((void *) ka, 0, PGSIZE);
    va = PGROUNDDOWN(va);
    if (mappages(p->pagetable, va, PGSIZE, ka, PTE_W|PTE_U|PTE_R) != 0) {
      kfree((void *)ka);
      p->killed = 1;
    }
  }
}
```

一个需要分配的内存地址应满足三个条件：1、位于进程的内存地址范围中（即小于 `p->sz`）；2、不是 guard page（根据 xv6 内存结构，栈的最低一页未被映射，用于捕捉栈溢出错误。懒分配不应对其分配物理内存、建立映射，否则无法通过 `stacktest` 测试）；3、不存在对应页表。据此，添加 `uvmislazyallocated()` 函数，判断给定虚拟地址是否为懒分配、但尚未真实分配的内存地址：

```c
int uvmislazyallocated(uint64 va) {
  pte_t *pte;
  struct proc *p = myproc();

  return va < p->sz
    && PGROUNDDOWN(va) != r_sp()
    && (((pte = walk(p->pagetable, va, 0)) == 0) || ((*pte & PTE_V) == 0));
}
```

使用上述两个函数，重写对 `usertrap()` 函数的修改：

```c
else if ((r_scause() == 15 || r_scause() == 13) && uvmislazyallocated(r_stval())) {
    uvmlazyallocate(r_stval());
}
```

较上一实验，加入了 `r_scause() == 13`，这是因为当 `scause` 寄存器为 13 时，说明是因为 load指令引起的 page fault，与 store 指令类似，都需要分配实际内存并映射。

与上一实验 `uvmunmap` 报错类似，需要取消一些检查。它们是 `uvmunmap()` 与 `uvmcopy()` 中的两项检测：

```c
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  // 省略
  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0) {
      continue;
    }
    if((*pte & PTE_V) == 0){
      continue;
    }
  // 省略
  }
}

int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  // 省略
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      continue;
    if((*pte & PTE_V) == 0)
      continue;
  // 省略
  }
}
```

修改 `sys_sbrk()`，使其支持参数为负的情况：

```c
uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if (n < 0) {
    uvmdealloc(myproc()->pagetable, addr, addr + n);
  }
  myproc()->sz += n;
  return addr;
}
```

为通过 `sbrkarg` 测试，需要解决内核与用户态之间拷贝数据时，懒加载未实际分配的问题：

```c
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  // 省略

  if (uvmislazyallocated(dstva))
    uvmlazyallocate(dstva);
  // 省略
}

int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;
  // 省略

  if (uvmislazyallocated(srcva))
    uvmlazyallocate(srcva);
  // 省略
}
```

编译运行 xv6，执行 `lazytests` 与 `usertests`，均输出 `ALL TESTS PASSED`，实验完成。
