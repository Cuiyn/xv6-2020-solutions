# Page Tables

## Print a page table

本实验要求实现一个函数 `vmprint()`，其参数为一个 `pagetable_t` 类型的值。该函数的功能是按如下格式输出 `pagetable`：

```
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

这是一个树状的 `pagetable` 形式，“..”表示当前 `PTE` 的深度。每一行展示了 `PTE` 的索引号、十六进制表示的位与物理地址。`vmprint()` 应在 `exec()` 执行完毕之前被调用，因此实验提示我们在 `kernel/exec.c` 文件的相应位置添加：

```c
if (p->pid == 1) vmprint(p->pagetable);
```

此外，还需在 `kernel/defs.h` 中注明此函数的定义。这里不再赘述。 

本实验难度分类为简单，只要在 `kernel/vm.c` 中实现 `vmprint()` 这一个函数即可。实验提示我们可以参考在同一文件中的 `freewalk()` 函数，其代码如下：

```c
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}
```

阅读此代码得知，`freewalk()` 函数提供了 `vmprint()` 函数的关键逻辑：如何判断一个 `pagetable` 是否为一个有效的 `PTE`、一个有效的 `PTE` 是否为最末层的 `PTE`。首先，若 `PTE` 的 `PTE_V` 为 1，则说明该 `PTE` 有效。若该 `PTE` 不可读、写、执行，则认为该 `PTE` 不是最末层的 `PTE`，否则认为是最末层的 `PTE`。这是因为，在 `xv6` 运行的 `Sv39 RISC-V` 环境下，每个进程虚拟地址的高 27 位用来确定 `PTE`，这 27 位地址又分为 3 组 9 位的地址，分别确定三层 `PTE` 的位置，其中只有最末层 `PTE` 指向物理内存，因此只有最末层 `PTE` 是可读、写或执行的。

具备上述背景知识后，易参照 `freewalk()` 函数实现 `vmprint()`： 

```c
void
vmprint_walk(pagetable_t pagetable, int level)
{
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      uint64 child = PTE2PA(pte);
      switch (level) {
      case 0: {
        printf("..");
        break;
      }
      case 1: {
        printf(".. ..");
          break;
      }
      case 2: {
        printf(".. .. ..");
          break;
      }
default:
        break;
      }
      printf("%d: pte %p pa %p\n", i, pte, child);
      vmprint_walk((pagetable_t)child, level + 1);
    } else if (pte & PTE_V) {
      uint64 child = PTE2PA(pte);
      printf(".. .. ..%d: pte %p pa %p\n", i, pte, child);
    }
  }
}

void
vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", pagetable);
  vmprint_walk(pagetable, 0);
}
```

与 `freewalk()` 类似，引入 `vmprint_walk()` 实现递归调用，对每个 `PTE` 的子地址进行判断。此外，为实现层级输出，传入 `level` 以告知当前函数处理地址的层数（最末层无需根据 `level` 判断）。根据提示，在 `printf()` 中使用 `%p` 即可输出内存地址（无需添加 `0x` 前缀），这里 `p` 是指针的缩写。

最后，还需要根据实验要求，在 `exec.c` 文件中添加对 `vmprint` 函数的调用。

```c
int
exec(char *path, char **argv)
{
  // 省略
  vmprint(p->pagetable);
  return argc;
  // 省略
}
```

至此本实验完成。 

## A kernel page table per process

本实验为第一个难度等级为困难的实验，但是实验同时提供了详细的提示，需要按照这些提示步步为营。


xv6 的设计是，每个用户进程在用户态使用各自独立的用户页表，在进入内核态时，则切换至内核页表，而这个内核页表是统一的。本实验要求为每一个进程提供一个独立的内核 `pagetable`，这样的好处是，进程执行内核相关操作时，能够完整地保留进程的内存映射关系，而无需再做指针转换。

首先，在 `kernel/proc.h` 的 `proc` 结构体定义中加入进程的内核页表字段：

```c
pagetable_t kernelpgtbl;
```

之后，修改 `kernel/vm.c` 中 `kvminit()` 函数，编写代码为每个进程产生内核 `pagetable`。这里的思路是，将此函数中的部分功能分出，以便提供为进程新建内核 `pagetable` 的功能。首先新建一个 `kvmmake()` 函数：

```c
pagetable_t
kvmmake()
{
  pagetable_t p = (pagetable_t) kalloc();
  memset(p, 0, PGSIZE);

  kvmmap(p, UART0, UART0, PGSIZE, PTE_R | PTE_W);
  kvmmap(p, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
  kvmmap(p, PLIC, PLIC, 0x400000, PTE_R | PTE_W);
  kvmmap(p, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);
  kvmmap(p, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);
  kvmmap(p, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
  return p;
}
```

其中， `kvmmap()` 函数通过创建 PTE 的方式将一个虚拟地址范围映射到一个物理地址范围。注意该函数不含对 `CLINT` 的映射，这是因为，`CLINT` 用于存储发生时钟中断时的一些额外信息，由于存取这些信息的过程都发生在内核启动过程，不受页表控制，所以这个区域无需映射。此外还需要修改 `kvmmap` 与 `kvmpa` 两个函数的签名（加入新参数 `pgtbl`），并将函数内的 `kernel_pagetable` 修改为 `pgtbl` 以便处理每个进程单独的内核页表。

之后修改 `kvminit()` 函数：

```c
void
kvminit()
{
  kernel_pagetable = kvmmake();
  // 这里需要映射 CLINT
  kvmmap(p, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
}
```

修改后， `kvminit()` 函数变成了全局内核页表的创建函数，此页表在内核启动过程中、或当前无进程运行时使用。

之后，修改进程初始化函数 `procinit()`，不再为每个进程分配内核栈。在下面的步骤中，变为创建进程的时候再创建内核栈。注释掉 `for` 循环中除 `initlock` 之外的内容即可。

之后修改 `kernel/proc.c` 文件的 `allocproc()` 函数，在此时创建进程的内核页表，并在内核页表上分配一个内核栈：

```c
// 省略
if(p->pagetable == 0){
  freeproc(p);
  release(&p->lock);
  return 0;
}
// 以下为添加内容
// 创建进程的内核页表
p->kernelpgtbl = kvmmake();
char *pa = kalloc();
if (pa == 0)
  panic("kalloc");
uint64 va = TRAMPOLINE - 2 * PGSIZE;
// 为 va 映射一块内存，大小为一个页表
mappages(p->kernelpgtbl, va, PGSIZE, (uint64)pa, PTE_R | PTE_W);
// 创建内核栈
p->kstack = va;
// 省略
```

此时，进程独立的内核页表创建完成，但还需要作相关修改，以使用户进程进入内核态后使用其自己的内核页表。修改 `kernel/proc.c` 文件的 `scheduler()` 函数，在 `swtch()` 切换进程前修改 `SATP`，保证进程执行期间用的是进程内核页表，切换后再修改 `SATP` 为全局内核页表：

```c
// 省略
p->state = RUNNING;
c->proc = p;
// 以下为添加内容
// 切换到进程独立的内核页表
w_satp(MAKE_SATP(p->kernelPageTable));
// 清除快表缓存
sfence_vma();
swtch(&c->context, &p->context);
// 调度完毕，切换回全局内核页表
kvminithart();
// 省略
```

之后，修改 `kernel/vm.c` 文件中的 `kvmpa()` 函数，保证其获取的是进程的内核页表，而不是全局内核页表：

最后要考虑进程结束后的资源释放。修改 `kernel/proc.c` 文件中的 `freeproc()` 函数，释放进程内核页表资源：

```c
// 省略
p->xstate = 0;
// 以下为添加内容
void *kstack_pa = (void *)kvmpa(p->kernelpgtbl, p->kstack);
kfree(kstack_pa);
p->kstack = 0;
proc_free_kernelpgtbl(p->kernelpgtbl);
p->kernelpgtbl = 0;
// 省略
```

新增的 `proc_free_kernel_pagetable()` 函数放在 `kernel/vm.c` 文件中，同样由 `freewalk` 函数修改而来：

```c
void
proc_free_kernelpgtbl(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    uint64 child = PTE2PA(pte);
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){ // 如果该页表项指向更低一级的页表
      // 递归释放低一级页表及其页表项
      kvm_free_kernelpgtbl((pagetable_t)child);
      pagetable[i] = 0;
    }
  }
  kfree((void*)pagetable); // 释放当前级别页表所占用空间
}
```

编译、运行系统，执行 `usertests`，测试完成后观察到输出内容含有 `ALL TESTS PASSED`，至此本实验完成。

## Simplify `copyin/copyinstr`

本实验又是一个难度分类为 hard 的任务，主要实验对象为内核的 `copyin` 函数。`copyin` 函数负责从用户内存中拷贝内容到内核内存中。该函数将用户指针指向的地址转换为物理地址，由此，内核能够直接索引到用户内存。实验要求为每个进程的内核页表（已在上一项实验中完成）添加用户映射，这样，`copyin` 函数（还有相关的字符串函数 `copyinstr`）就可以直接使用用户指针。

为实现实验目的，需要在每当内核对用户页表进行修改时，将同样的修改应用到进程的内核页表上。

首先实现两个工具方法，用于后续的调用：

```c
// 将源页表的一部分映射关系复制到目的页表中
// 仅复制页表项，而不复制实际内存
int
kvmcopymappings(pagetable_t src, pagetable_t dst, uint64 start, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  // PGROUNDUP: 对齐页边界，防止 remap
  for(i = PGROUNDUP(start); i < start + sz; i += PGSIZE){
    if((pte = walk(src, i, 0)) == 0)
      panic("kvmcopymappings: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("kvmcopymappings: page not present");
    pa = PTE2PA(*pte);
    // `& ~PTE_U` 表示将该页的权限设置为非用户页
    // 必须设置该权限，RISC-V 中内核是无法直接访问用户页的。
    flags = PTE_FLAGS(*pte) & ~PTE_U;
    if(mappages(dst, i, PGSIZE, pa, flags) != 0){
      uvmunmap(dst, 0, i / PGSIZE, 0);
      return -1;
    }
  }

  return 0;
}

// 与 uvmdealloc 功能类似，将程序内存从 oldsz 缩减到 newsz。但区别在于不释放实际内存
// 用于内核页表内程序内存映射与用户页表程序内存映射之间的同步
uint64
kvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 0);
  }

  return newsz;
}
```

此外，实验要求在 `exec()` 中加入检查，以防止程序内存超过 `PLIC`：

```c
// 省略
if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz)) == 0)
  goto bad;
// 以下为添加内容
if(sz1 >= PLIC)
  goto bad;

// 省略
```

之后在每个修改进程用户页表的程序处，将对应修改同步到进程的内核页表中。一共需要修改四个函数：`fork()`、`exec()`、`growproc()` 与 `userinit()`。

```c
int
fork()
{
// 省略
// 修改 if 条件
if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0 ||
   kvmcopymappings(np->pagetable, np->kernelpgtbl, 0, p->sz) < 0){
  freeproc(np);
  release(&np->lock);
  return -1;
}
np->sz = p->sz;
// 省略
}
```

```c
int
exec(char *path, char **argv)
{
// 省略
for(last=s=path; *s; s++)
  if(*s == '/')
    last = s+1;
safestrcpy(p->name, last, sizeof(p->name));
// 清除内核页表中对程序内存的旧映射
uvmunmap(p->kernelpgtbl, 0, PGROUNDUP(oldsz)/PGSIZE, 0);
// 重建新映射
kvmcopymappings(pagetable, p->kernelpgtbl, 0, sz);
// 省略
}

```c
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    uint64 newsz;
    if((newsz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
    // 内核页表中的映射同步扩大
    if(kvmcopymappings(p->pagetable, p->kernelpgtbl, sz, n) != 0) {
      uvmdealloc(p->pagetable, newsz, sz);
      return -1;
    }
    sz = newsz;
  } else if(n < 0){
    uvmdealloc(p->pagetable, sz, sz + n);
    // 内核页表中的映射同步缩小
    sz = kvmdealloc(p->kernelpgtbl, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}
```

```c
void
userinit(void)
{
  // 省略

  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;
  // 以下为添加内容
  kvmcopymappings(p->pagetable, p->kernelpgtbl, 0, p->sz); // 同步程序内存映射到进程内核页表中
  // 省略
}
```

最后，对 `copyin` 与 `copyinstr` 函数，删除其中全部内容，替换为 `copyin_new` 与 `copyinstr_new` 的调用即可。

至此本章实验全部完毕。执行 `make grade`，观察到成功通过所有测试。
