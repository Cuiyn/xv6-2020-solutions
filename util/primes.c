#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAXNUM 36

int
main(int argc, char *argv[])
{
  int p[2];
  int number[MAXNUM];
  int i, count;
  if (argc != 1) {
    printf("primes: do not need parameters.\n");
    exit(1);
  }

  for (i = 2; i < 36; ++i) {
    number[i - 2] = i;
  }

  while (1) {
    pipe(p);
    if (number[0] == 35) {
      printf("prime %d\n", number[0]);
      break;
    }
    int pid = fork();
    if (pid == 0) {
      // son
      close(p[0]);
      printf("prime %d\n", number[0]);
      count = number[0];
      for (i = 1; i < MAXNUM; ++i) {
        if (number[i] % count != 0) {
          write(p[1], &number[i], sizeof(int));
        }
        if (number[i] == 35) {
          break;
        }
      }
      close(p[1]);
      exit(0);
    } else {
      // father
      close(p[1]);
      i = 0;
      wait((int *) 0);
      while (read(p[0], &count, sizeof(int))) {
        number[i] = count;
        ++i;
        if (i == MAXNUM) {
          break;
        }
      }
      close(p[0]);
    }
  }
  exit(0);
}
