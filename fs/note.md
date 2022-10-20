# File System

本实验将为 xv6 增加大文件与符号链接（symbolic link，又称为“软链接”）支持。

## Large files

在 xv6 中，`inode` 共存储 12 个直接块编号（direct block number）与 1 个间接块编号（indirect block number）。与 page table 的结构类似，直接块用于存储文件数据，间接块指向一个包含 256 个块编号的块。xv6 定义一个块大小为 1024 字节，因此支持的最大文件大小为 `(256+12)*1024`，即 268 KB。

实验要求增加 xv6 支持的最大文件大小。实验提供了 `bigfile` 测试程序，此程序生成一个可能范围内最大的文件，并报告大小。实验要求增加最大文件大小至 65803 KB （即块数为 65803），这一数字可通过 `256*256+256+11` 得到。方案是：11 个直接块，1 个一级间接块（指向一个包含 256 个块编号的块）和 1 个二级间接块（指向一个包含 256 个块编号的块，其中每个块编号又指向一个包含 256 个块编号的块）。

首先，修改头文件 `kernel/fs.h` 中关于直接块与最大文件大小的定义：

```c
// 直接块改为 11，将原方案的 1 个块挪作间接块
#define NDIRECT 11
// 此数值不变，为 256
#define NINDIRECT (BSIZE / sizeof(uint))
// 修改最大文件大小
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT * NINDIRECT)
```

同时修改 `kernel/fs.h` 与 `kernel/file.h` 中 `dinode` 与 `inode` 结构体的定义，`addr` 长度为 `NDIRECT+2`，保证 `inode` 仍为 64 字节。

之后修改 `kernel/fs.c`。根据实验提示，需要修改的函数为 `bmap()` 与 `itrunc()`。首先分析 `bmap()`，程序已经实现了一级间接块的逻辑，参考补充即可。

```c
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }
  // 新增
  bn -= NINDIRECT;

  if(bn < NINDIRECT * NINDIRECT){
    // 分别计算一级索引与二级索引
    int first_index = bn / NINDIRECT;
    int second_index = bn % NINDIRECT;

    if ((addr = ip->addrs[NDIRECT+1]) == 0)
      ip->addrs[NDIRECT+1] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint *)bp->data;
    if ((addr = a[first_index]) == 0) {
      a[first_index] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);

    bp = bread(ip->dev, addr);
    a = (uint *)bp->data;
    if ((addr = a[second_index]) == 0) {
      a[second_index] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }
  // 新增部分结束

  panic("bmap: out of range");
}
```

`itrunc()` 负责释放文件对应块。新增二级间接块释放逻辑，保证函数释放文件占用的所有块：

```c
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]) {
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  // 新增
  if(ip->addrs[NDIRECT+1]){
    bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      // 如果对应的一级间接块存在，则遍历其指向的所有二级间接块，若存在内容则释放
      if(a[j]){
        struct buf *second_bp = bread(ip->dev, a[j]);
        uint *second_a = (uint*)second_bp->data;
        for(int k = 0; k < NINDIRECT; k++){
          if(second_a[k])
            bfree(ip->dev, second_a[k]);
        }
        brelse(second_bp);
        bfree(ip->dev, a[j]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }
  // 新增结束

  ip->size = 0;
  iupdate(ip);
}
```

编译运行 xv6，执行 `bigfile` 测试，成功生成占用 65803 个块的文件。执行 `usertests` 全部通过。实验结束。

## Symbolic links

实验要求为 xv6 实现符号链接。这里首先对硬链接与软链接进行区分：

- 硬链接：与原文件非常相似，它们的 `inode` 相同。删除原文件后，`inode` 指向的文件不会立即释放，硬链接仍可正常使用。
- 软链接：保存原文件的绝对路径。它是另外一种文件，在硬盘上有独立的区块。访问时将路径替换为原文件。

实验要求实现 `symlink(char *target, char *path)` 系统调用。实验同时提供了 `symlinktest` 程序供测试。

首先为新添加的系统调用添加一系列声明：

```c
// kernel/syscall.h
// 分配新的系统调用号
#define SYS_symlink 22
```

```c
// kernel/syscall.c
extern uint64 sys_symlink(void);

[SYS_symlink] sys_symlink,
```

```c
// user/usys.pl
entry("symlink");
```

此外，为了让测试程序 `symlinktest` 能够正常运行，需要在 `Makefile` 中添加对它的编译：

```shell
ifeq ($(LAB),fs)
 UPROGS += \
    $U/_bigfile\
    $U/_symlinktest
endif
```

这时 `symlinktest` 应该能够正常编译了，但由于还没有实现新的系统调用，它并不能正常工作。符号链接的系统调用主要通过修改 `kernel/sysfile.c` 文件实现。首先，编写函数 `sys_symlink()`：

```c
uint64
sys_symlink(void)
{
  char target[MAXPATH], path[MAXPATH];
  struct inode *ip;

  if(argstr(0, target, MAXPATH) < 0)
    return -1;

  begin_op();
  // 仿照其它系统调用，读取参数并判断合法性，并创建对应的 inode
  if(argstr(1, path, MAXPATH) < 0 || (ip = create(path, T_SYMLINK, 0, 0)) == 0){
    end_op();
    return -1;
  }
  // 将 target 指定的目录写入 inode
  // 第一个 0 指地址来自内核虚拟地址空间
  // 第二个 0 指偏移量为 0
  if(writei(ip, 0, (uint64)target, 0, MAXPATH) == -1)
  {
    end_op();
    return -1;
  }
  // 由于 create 对 inode 加了锁，这里需要进行释放
  iunlockput(ip);
  end_op();
  return 0;
}
```

之后修改 `sys_open()` 函数，实现对符号链接文件的操作。

```c
if(omode & O_CREATE){
  ip = create(path, T_FILE, 0, 0);
  if(ip == 0){
    end_op();
    return -1;
  }
} else {
  // 从这里开始修改
  // 规定最多循环次数，实验提示可设置为 10
  // 若深度超过这一次数，则可认为符号链接之前存在环路
  int symlink_loop = 10;
  while (1) {
    ip = namei(path);
    if (ip == 0) {
      end_op();
      return -1;
    }
    ilock(ip);
    // 若 ip 为符号链接，且没有指定 O_NOFOLLOW
    // 寻找链接指定的文件
    if (ip->type == T_SYMLINK && (omode & O_NOFOLLOW) == 0) {
      if (symlink_loop == 0) {
        iunlockput(ip);
        end_op();
        return -1;
      }
      if (readi(ip, 0, (uint64)path, 0, MAXPATH) < 0) {
        iunlockput(ip);
        end_op();
        return -1;
      }
      iunlockput(ip);
    } else {
      break;
    }
    symlink_loop--;
  }
}
// 修改结束
```

编译运行 xv6，执行 `symlinktest` 测试，成功执行。执行 `usertests` 全部通过。实验结束。
