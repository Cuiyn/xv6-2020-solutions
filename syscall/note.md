# System Calls

本节内容

## System call tracing

本实验要求实现一个系统调用追踪功能，以便后续实验的 debug。具体而言，实验要求实现
一个名叫 `trace` 的系统调用，它需要传入一个 `int` 类型的参数 `mask`，该参数的二
进制位标记了需要追踪的系统调用编号。例如，`trace(1 << SYS_FORK)` 追踪了 `fork`，
其中 `SYS_FORK` 是预先定义的常量。实验提供了 `trace` 的用户态实现代码，关键部分为：

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

之后为实现部分。因为 `trace` 与系统调用密切相关，首先从 `kernel/syscall.c` 入手。
观察到，`void syscall(void)` 函数是用于调用系统调用的函数，其代码为：

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

其中的 `proc` 定义在 `kernel/proc.h` 中，参考 xv6 手册可知 `trap` 的含义及作用，
其中 `a7` 保存的内容即为系统调用编号，`a0` 保存的内容为系统调用的返回值。有了这
些知识，容易在此基础上补充实现代码：

```c
if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
  p->trapframe->a0 = syscalls[num]();
  if (p->mask & (1 << num))
    printf("%d: syscall %s -> %d\n", p->pid, syscalls_name[num], p->trapframe->a0);
} else {
  // 省略
}
```

使用位运算的相关技巧，判断当然系统调用编号是否在 `mask` 中进行了标注，并对需要追
踪的系统调用，输出当前进程号、系统调用名称与返回值。这里的 `syscalls_name` 是一
个静态数组，用于从系统调用编号获取系统调用名称，需要参考同一文件中的 `static
uint64 *syscalls[]` 自行实现。

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

下一步是如何将 `mask` 引入进程结构体 `proc`。直接在 `kernel/proc.h` 中的 `proc`
定义中添加 `int mask;` 即可，之后还需要在 `kernel/proc.c` 中的 `fork()` 函数中添
加代码，以将父进程的 `mask` 赋给子进程。

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

参考该文件中的其它实现，采用 `argint` 函数读入 `mask` 。这里只需将用户态调用时传
入的 `mask` 赋给当前进程。至此，本实验完成。
