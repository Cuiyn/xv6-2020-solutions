# Traps

## RISC-V assembly

本实验要求分析 `user/call.c` 编译形成的汇编代码，并回答问题。

- 问题：哪些寄存器保存函数的参数？例如，哪一个寄存器保存了 `main` 调用 `printf` 函数时的参数 `13`？
- 答案：`a0-a7`，由 RISC-V 汇编格式决定；`a2`，易由汇编代码 `li a2,13` 推得。

- 问题：`main` 中，调用 `f` 函数所对应的汇编代码在哪里？调用 `g` 函数呢？（提示，编译器可能会使用内联函数）
- 答案：由于编译器使用了内联函数，调用这两个函数的汇编代码不存在。由汇编代码 `li a1,12` 可知，函数调用结果 `12` 直接被写入程序，而没有通过函数调用获得。

- 问题：函数 `printf` 的地址在哪里？
- 答案：`0x64a`，`call.asm` 文件直接给出。

- 问题：在 `main` 中，使用 `jalr` 跳转到 `printf` 之后，`ra` 的值是什么？
- 答案：`0x38`，即函数的返回地址。

- 问题：运行下列代码。
  ```c
  unsigned int i = 0x00646c72;
  printf("H%x Wo%s", 57616, &i);
  ```
  输出是什么？这一输出依赖于 RISC-V 的小端序实现，如果 RISC-V 是大端序的，要得到同样的输出，需要如何设置 `i`？需要把 `57616` 设置为不同的值吗？
- 答案：`He110 World`；需要将 `i` 设置为 `0x726c6400`；不需要修改 `57616`，因为它的十六进制表示永远是 `0xe110`，与端序无关。

- 问题：在下面的代码中，在 `y=` 之后会打印什么？（提示，答案不是一个确定的值）为什么？
  ```c
  printf("x=%d y=%d", 3);
  ```
- 答案：会打印寄存器 `a2` 的值，此值受 `printf` 被调用之前代码的影响，不能确定。

## Backtrace

在 debug 时，回溯一般比较有用。回溯（backtrace）指在函数发生错误的地方，栈上函数调用的列表。实验要求实现 `backtrace()` 函数。

实验题目给出了足够的提示，大幅降低了实验难度。首先，根据提示，在 `kernel/riscv.h` 中添加函数：

```c
static inline uint64
r_fp()
{
  uint64 x;
  asm volatile("mv %0, s0" : "=r" (x) );
  return x;
}
```

根据课程，`fp` 指向当前栈帧的开始地址，`sp` 指向当前栈帧的结束地址。但是，由于栈从高地址向低地址生长，`fp` 比 `sp` 高。提示说明了 xv6 的栈结构：栈帧中从高到低第一个 8 字节 `fp-8` 是 return address，即当前调用层应该返回到的地址；栈帧中从高到低第二个 8 字节 `fp-16` 是 previous address，即上一层栈帧的 `fp` 地址。据此，可以实现 `backtrace()`：
```c
// kernel/printf.c
void
backtrace(void)
{
  uint64 fp = r_fp();
  printf("backtrace:\n");
  // 判断是否到达栈帧结束
  while(fp != PGROUNDUP(fp))
  {
    printf("%p\n", *(uint64 *)(fp - 8));
    fp = *(uint64 *)(fp - 16);
  }
}
```

之后，在 `sys_sleep()` 中添加对 `backtrace()` 的调用：
```c
// kernel/sysproc.c
uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  backtrace(); 
  // 省略
}
```

在 `kernel/defs.h` 中添加 `backtrace()` 的定义。编译运行，在终端中执行 `bttest`，成功输出回溯结果，实验完成。

## Alarm

实验要求为 xv6 添加功能，使得在进程使用 CPU 时间时，能够定期发出警告。这一功能对于想要限制占用 CPU 时间的计算型进程，或者对于想要计算但又想采取一些定期行动的进程，可能是很有用的。广泛地说，实验将实现一个原始形式的用户级中断/故障处理程序；例如，可以使用类似的方式来处理应用程序中的页面故障。

具体地，实验要求实现一个新的系统调用 `sigalarm(interval, handler)`。如果某个应用调用了 `sigalarm(n, fn)`，则应用消耗每 `n` 个 CPU 时间片（tick），内核应调用应用函数 `fn`。当 `fn` 返回时，应用应该恢复到它离开时的状态。如果应用调用了 `sigalarm(0, 0)`，则内核应该停止生成定期的警告。

`alarmtest` 实现了 `test0`、`test1` 与 `test2` 三项测试。首先在 `Makefile` 添加对 `alarmtest` 的编译。根据提示，在 `user/user.h` 中添加系统调用定义：
```c
// user/user.h
int sigalarm(int ticks, void (*handler)());
int sigreturn(void);
```

提示指出，此系统调用的一些参数保存在 `proc` 结构体中。因此，在 `proc` 结构体的定义中添加相应内容：
```c
struct proc {
  // 省略
  // 保存系统调用的两个参数
  int alarm_interval;
  void (*alarm_handler)();
  // 当前的 tick 数量，由 alarm_interval 递减
  int alarm_ticks;
  // 中断前保存 trapframe，以便于后续恢复
  struct trapframe *alarm_trapframe;
  // 记录是否有未返回的 alarm
  int alarm_goingoff;
}
```

在 `kernel/sysproc.c` 与 `kernel/trap.c` 中实现两个系统调用，并根据提示位置，添加系统调用相关定义。
```c
// kernel/sysproc.c
uint64
sys_sigalarm(void)
{
  int ticks;
  uint64 handler;
  if(argint(0, &ticks) < 0)
    return -1;
  if(argaddr(1, &handler) < 0)
    return -1;
  return sigalarm(ticks, (void (*)())handler);
}

uint64
sys_sigreturn(void)
{
  return sigreturn();
}
```
```c
// kernel/trap.c
int sigalarm(int ticks, void (*handler)())
{
  struct proc *p = myproc();
  p->alarm_interval = ticks;
  p->alarm_handler = handler;
  p->alarm_ticks = ticks;
  return 0;
}

int sigreturn(void)
{
  // 恢复时钟中断前的状态
  struct proc *p = myproc();
  p->trapframe = p->alarm_trapframe;
  p->alarm_goingoff = 0;
  return 0;
}
```
```c
// kernel/defs.h
// trap.c
// 省略
int             sigalarm(int ticks, void (*handler)());
int             sigreturn(void);
```
```c
// kernel/syscall.c
// 省略
extern uint64 sys_sigalarm(void);
extern uint64 sys_sigreturn(void);
// 省略
[SYS_sigalarm]   sys_sigalarm,
[SYS_sigreturn]   sys_sigreturn,
// 省略
```
```c
// kernel/syscall.h
// 省略
#define SYS_sigalarm   22
#define SYS_sigreturn  23
```
```perl
# user/usys.pl
# 省略
entry("sigalarm");
entry("sigreturn");
```

在 `kernel/proc.c` 中添加对上述新变量的初始化与释放操作：
```c
// kernel/proc.c
static struct proc*
allocproc(void)
{
  // 省略
  // Allocate a trapframe page for alarm_trapframe.
  if((p->alarm_trapframe = (struct trapframe *)kalloc()) == 0){
    release(&p->lock);
    return 0;
  }
  
  p->alarm_interval = 0;
  p->alarm_handler = 0;
  p->alarm_ticks = 0;
  p->alarm_goingoff = 0;
  
  // 省略
}


static void
freeproc(struct proc *p)
{
  // 省略
  if(p->alarm_trapframe)
    kfree((void*)p->alarm_trapframe);
  p->alarm_trapframe = 0;
  // 省略
  p->alarm_interval = 0;
  p->alarm_handler = 0;
  p->alarm_ticks = 0;
  p->alarm_goingoff = 0;

  p->state = UNUSED;
}
```

最后，在 `usertrap()` 函数中，实现时钟机制。修改 `if(which_dev == 2)` 判断。
```c
// kernel/trap.c
void
usertrap(void)
{
  // 省略
  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
  {
    if(p->alarm_interval > 0) {
      p->alarm_ticks --;
      if (p->alarm_ticks >= 0 && !p->alarm_goingoff) {
        p->alarm_ticks = 0;
        p->alarm_trapframe = p->trapframe;
        p->trapframe->epc = (uint64) p->alarm_handler;
        p->alarm_goingoff = 1;
      }
    }
    yield();
  }
  // 省略
}
```

由此，在发生时钟中断时，如果有已经设置的时钟（即 `alarm_interval != 0`），则递减 `alarm_ticks`。当 `alarm_ticks` 小于或等于 0 时，若没有正在返回的时钟，则应触发时钟。触发前，应先通过 `p->alarm_trapframe = p->trapframe` 将原来的程序信息保存，再通过修改 PC 寄存器的值，使得下一条指令为 `alarm_handler`，待其结束后，再恢复原来的程序（`sigreturn` 函数中的 `*p->trapframe = *p->alarm_trapframe`）。对原有程序来说，这一过程是透明的。
