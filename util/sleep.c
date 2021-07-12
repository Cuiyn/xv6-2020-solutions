#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  // int i = 0, n = 0;

  if (argc != 2) {
    printf("sleep: need a parameter.\n");
    exit(1);
  }
  sleep(atoi(argv[1]));
  exit(0);
}
