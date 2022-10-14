# Multithreading

本实验要求在用户态线程库实现线程切换，使用多线程加速程序，并实现一个同步屏障。

## Uthread: switching between threads

实验要求补全 `user/uthread.c` 与 `user/uthread_switch.S`，设计并实现用户态线程的上下文切换机制。

由于 xv6 已经提供了内核级别的线程切换，本实验可参考。例如 `user/uthread_switch.S` 可完全复制 `kernel/swtch.S` 的实现。C 语言代码调用汇编函数 `uthread_switch()` 时，caller-save registers 已被保存，因此汇编代码不需考虑这些寄存器。另一方面，C 语言代码也难以直接操作诸如 `ra` 与 `sp` 之类寄存器，因此需要使用汇编代码。

之后修改 `user/uthread.c`。首先，参考内核实现方法 `kernel/proc.h`，为用户态的每个线程添加一个上下文结构，用于保存寄存器数据：

```c
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

struct thread {
  char       stack[STACK_SIZE]; /* the thread's stack */
  int        state;             /* FREE, RUNNING, RUNNABLE */
  // 添加 context 结构体
  struct context context;
};
```

在 `thread_schedule()` 函数中，调用汇编函数 `thread_switch()` 进行线程上下文切换：

```c
if (current_thread != next_thread) {
  next_thread->state = RUNNING;
  t = current_thread;
  current_thread = next_thread;
  // 添加代码
  thread_switch(&t->context, &next_thread->context);
} else
  next_thread = 0;
```

在 `thread_create()` 函数中，为线程的返回地址与初始栈指针赋值：

```c
void 
thread_create(void (*func)())
{
  struct thread *t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->state = RUNNABLE;
  // 添加代码
  t->context.ra = (uint64)func;
  // 栈的使用从高地址开始
  t->context.sp = (uint64)&t->stack + (STACK_SIZE - 1);
}
```

编译运行 xv6，执行 `uthread` 程序，正常运行。实验结束。

## Using threads

实验提供了一个多线程版的哈希表程序，但由于 race-condition，多个线程运行时，会丢失数据。实验要求保证数据安全的前提下，体现多线程的速度优势。实验打分程序会对数据安全与速度进行检查，其中要求 2 线程运行时，速度应至少达到单线程的 1.25 倍。

首先，定义并初始化锁：

```c
pthread_mutex_t lock;
// 省略
int
main(int argc, char *argv[])
{
  // 省略
  pthread_mutex_init(&lock, NULL);
  // 省略
}
```

数据丢失的一个可能场景是，线程 A 试图在bucket 末尾插入新键值对，在其完成前，线程 B 完整完成了一次插入操作。此时，线程 A 没有发觉到 bucket 末尾已经改变，将新键值对的 `next` 指向了 bucket 非末尾的元素，此时线程 A 插入的键值对丢失。

经过上述分析，通过阅读程序代码，发现 `put` 与 `get` 操作是分开进行的，因此只需对插入操作进行加锁即可：

```c
if(e){
  // update the existing key.
  e->value = value;
} else {
  // the new is new.
  pthread_mutex_lock(&lock);
  insert(key, value, &table[i], table[i]);
  pthread_mutex_unlock(&lock);
}
```

运行程序，分别指定线程数为 1 与 2，观察到线程数为 2 时，速度提升近 2 倍。执行 `make grade` 成功通过。实验结束。

注意，此方法能够最大限度提升速度，但并不完美，若 `put` 与 `get` 操作同时并行运行，只对插入操作加锁，还是可能会产生错误（例如，线程 A 在 bucket 中寻找某个 key 时，线程 B 刚刚将这个 key 插入到 bucket 末尾，此时线程 B 无法找到新增加的 key）。

## Barrier

实验要求实现线程同步屏障。当线程调用同步屏障函数 `barrier()` 时，函数判断是否所有线程均已进入屏障。如果不满足，则线程进入睡眠状态。如果均已进入，则唤醒所有线程，round 计数加 1。

“判断线程是否均进入屏障”需要保证原子性，否则可能导致 lost wake-up 问题。例如，线程 A 准备唤醒所有线程，进入下一轮；线程 B 之后进入睡眠。此时线程 B 应被唤醒却没有成功，导致线程 B 保持睡眠状态，程序死锁。

```c
static void 
barrier()
{
  // 加锁，保证原子性
  pthread_mutex_lock(&bstate.barrier_mutex);
  bstate.nthread ++;
  if (bstate.nthread == nthread) {
    // 满足条件，进入下一轮
    bstate.nthread = 0;
    bstate.round ++;
    pthread_cond_broadcast(&bstate.barrier_cond);
  } else {
    // 不满足条件，线程睡眠
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  }
  pthread_mutex_unlock(&bstate.barrier_mutex);
}
```

编译运行程序，程序正常运行。执行 `make grade` 成功通过。实验结束。
