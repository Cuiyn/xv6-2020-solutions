#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int p[2];
  char c[8];
  if (argc != 1) {
    printf("pingpong: do not need parameters.\n");
    exit(1);
  }
  pipe(p);
  if (fork() == 0) {
    // son
    read(p[0], c, 8);
    printf("%d: received %s\n", getpid(), c);
    close(p[0]);
    write(p[1], "pong", 4);
    close(p[1]);
    exit(0);
  } else {
    write(p[1], "ping", 4);
    wait(0);
    read(p[0], c, 8);
    printf("%d: received %s\n", getpid(), c);
    close(p[0]);
    close(p[1]);
    exit(0);
  }
}
