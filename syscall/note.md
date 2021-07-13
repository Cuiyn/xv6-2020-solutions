# System Calls

本节实验的代码较为分散，因此不再提供源文件，而改为辅以代码的思路描述。

## System call tracing

本实验要求实现一个系统调用追踪功能，以便后续实验的 debug。具体而言，实验要求实现一个名叫 `trace` 的系统调用，它需要传入一个 `int` 类型的参数 `mask`，该参数的二进制位标记了需要追踪的系统调用编号。例如，`trace(1 << SYS_FORK)` 追踪了 `fork`，其中 `SYS_FORK` 是预先定义的常量。实验提供了 `trace` 的用户态实现代码，关键部分为：

```c
if (trace(atoi(argv[1])) < 0) {
  fprintf(2, "%s: trace failed\n", argv[0]);
  exit(1);
}
```

在完成该实验的实现代码前，需要补足相关的定义。根据提供的 tips，有以下几项：

1. `user/user.h` 中声明用户态的系统调用函数 `int trace(int);`。
2. `user/usys.pl` 中添加 `entry("trace");` ，这是为了执行 `make` 命令的时候，能
   够生成 `user/usys.S` 作为系统调用接口。
3. `kernel/syscall.h` 中添加 `trace` 的系统调用编号。

之后为实现部分。因为 `trace` 与系统调用密切相关，首先从 `kernel/syscall.c` 入手。观察到，`void syscall(void)` 函数是用于调用系统调用的函数，其代码为：

```c
void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    p->trapframe->a0 = syscalls[num]();
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

其中的 `proc` 定义在 `kernel/proc.h` 中，参考 xv6 手册可知 `trap` 的含义及作用，其中 `a7` 保存的内容即为系统调用编号，`a0` 保存的内容为系统调用的返回值。有了这些知识，容易在此基础上补充实现代码：

```c
if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
  p->trapframe->a0 = syscalls[num]();
  if (p->mask & (1 << num))
    printf("%d: syscall %s -> %d\n", p->pid, syscalls_name[num], p->trapframe->a0);
} else {
  // 省略
}
```

使用位运算的相关技巧，判断当前系统调用编号是否在 `mask` 中进行了标注，并对需要追踪的系统调用，输出当前进程号、系统调用名称与返回值。这里的 `syscalls_name` 是一个静态数组，用于从系统调用编号获取系统调用名称，需要参考同一文件中的 `static uint64 *syscalls[]` 自行实现。

```c
static char* syscalls_name[] = {
  [SYS_fork]    "fork",
  [SYS_exit]    "exit",
  [SYS_wait]    "wait",
  [SYS_pipe]    "pipe",
  [SYS_read]    "read",
  [SYS_kill]    "kill",
  [SYS_exec]    "exec",
  [SYS_fstat]   "fstat",
  [SYS_chdir]   "chdir",
  [SYS_dup]     "dup",
  [SYS_getpid]  "getpid",
  [SYS_sbrk]    "sbrk",
  [SYS_sleep]   "sleep",
  [SYS_uptime]  "uptime",
  [SYS_open]    "open",
  [SYS_write]   "write",
  [SYS_mknod]   "mknod",
  [SYS_unlink]  "unlink",
  [SYS_link]    "link",
  [SYS_mkdir]   "mkdir",
  [SYS_close]   "close",
  [SYS_trace]   "trace",
};
```

下一步是如何将 `mask` 引入进程结构体 `proc`。直接在 `kernel/proc.h` 中的 `proc`定义中添加 `int mask;` 即可，之后还需要在 `kernel/proc.c` 中的 `fork()` 函数中添加代码，以将父进程的 `mask` 赋给子进程。

```c
np->mask = p->mask;
```

最后，实现 `kernel/sysproc.c` 中的 `sys_trace()` 函数：

```c
uint64
sys_trace(void)
{
  int mask;
  if (argint(0, &mask) < 0)
    return -1;
  myproc()->mask = mask;
  return 0;
}
```

参考该文件中的其它实现，采用 `argint` 函数读入 `mask` 。这里只需将用户态调用时传入的 `mask` 赋给当前进程。至此，本实验完成。

## Sysinfo

本实验要求实现一个名为 `sysinfo` 的系统调用，其功能是通过一个叫做 `sysinfo` 的结构体，给出系统当前的剩余内存与进程数。

首先，与上一个实验相同，需要补足新系统调用的定义，此处不再赘述。

`sysinfo` 需要实现两大功能：统计剩余内存与统计进程总数。其中个人认为进程更易实现，因此先从进程入手分析。

观察到， `kernel/proc.c` 文件定义与进程相关的内容，我们从该文件开始。文件中已经实现了保存有系统所有进程信息的数组：

```c
struct proc proc[NPROC];
```

因此，只需要遍历这个数组，并根据实验需要，统计所有状态不为 `UNUSED` 的元素即可。实现代码为：

```c
uint64
proccount()
{
  struct proc *p;
  uint64 count = 0;
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state != UNUSED)
      count ++;
  }
  return count;
}
```

需要注意返回值类型应与 `sysinfo` 结构体中定义的 `nproc` 类型一致，即为 `uint64`。

之后来讨论剩余内存的统计方法，据题目提示，相关代码在 `kernel/kalloc.c` 文件中。从 `kinit` 函数中可以发现，系统初始化内存的代码为：

```c
freerange(end, (void*)PHYSTOP);
```

其含义是：对从 `end` 开始到 `PHYSTOP` 结束的内存地址，使用 `freerange` 函数。其中，根据 `kernel/memlayout.h` 文件可知，`end` 是内核占用内存后的第一个地址，`PHYSTOP` 是内核占用内存的结束地址。而 `freerange` 函数又进一步调用了 `kfree` 函数。通过阅读 `kfree` 函数代码可发现一个关键值：`kmem.freelist`，它是一个保存了当前可用内存页的链表。因此，只需一直调用它的 `next`，统计其中元素的数量即为当前系统可用内存页数。根据上述分析，统计剩余内存的代码为：

```c
uint64
freecount()
{
  struct run *r;
  int count = 0;

  r = kmem.freelist;
  while (r) {
    count ++;
    r = r->next;
  }
  return count * PGSIZE;
}
```

此处需要注意，这里统计的是内存页数而不是内存大小，因此需要乘 `PGSIZE`。如下的实现不能返回正确结果（执行 `sysinfotest` 比正确值多 228 个内存页），猜测可能是因为在系统不断分配、回收内存的过程中，从 `kmem.freelist` 开始到物理内存结尾的内存中，可能有被系统分配出去、变为不可用的内存。

```c
return PHYSTOP - (uint64) r;
```

最后，我们需要实现从内核态向用户态返回数据的 `sys_sysinfo` 函数。此处的关键在于 `copyout` 函数的正确调用，该函数在 `kernel/defs.h` 文件中定义，在 `kernel/kalloc.c` 中实现：

```c
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len);
```

其注释告诉我们，该函数将长度为 `len` 的数据拷贝到给定 `pagetable` 的虚地址 `dstva` 中。因此，`sys_sysinfo` 函数应首先从用户态调用中获取结构体 `sysinfo` 的内存地址，之后再调用 `copyout` 函数将获取到的信息复制到此地址中。代码如下： 

```c
extern uint64 freecount();
extern uint64 proccount();

uint64
sys_sysinfo(void)
{
  struct sysinfo info;
  struct proc *p = myproc();
  uint64 addr;

  if(argaddr(0, &addr) < 0)
    return -1;

  info.freemem = freecount();
  info.nproc = proccount();

  if (copyout(p->pagetable, addr, (char *) &info, sizeof(info)) < 0)
    return -1;
  return 0;
}
```

至此，实验代码全部完成。
