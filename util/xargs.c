#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"
#define MAXLINE 1024
int main(int argc, char *argv[])
{
  char line[MAXLINE];
  char *cmd = argv[1];
  char* params[MAXARG];
  int i = 0, j = 0, params_index = 0, size = 0;

  for (i = 1; i < argc; i++) params[params_index++] = argv[i];

  while((size = read(0, line, sizeof(line))) > 0) {
    if (fork() == 0) {
      for (i = 0; i < size; i++) {
        for (j = i + 1; j < size; ++j) {
          if (line[j] == ' ' || line[j] == '\n') {
            char * param = malloc(sizeof(line));
            memcpy(param, line + i, sizeof(char) * (j - i));
            i = j + 1;
            params[params_index++] = param;
          }
        }
      }

      exec(cmd, params);
      for (i = argc; i < params_index; ++i) {
        free(params[i]);
      }

      exit(0);
    } else
      wait(0);
  }
  exit(0);
}
