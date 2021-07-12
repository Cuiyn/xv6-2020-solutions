#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char*
fmtname(char *path)
{
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  return p;
}

void findStrFromPath(char *path, char *str)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }
  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s.\n", path);
    close(fd);
    return;
  }
  switch(st.type){
  case T_DIR:
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if (strcmp(fmtname(buf), "..") != 0 && strcmp(fmtname(buf), ".") != 0) {
        findStrFromPath(buf, str);
      }
    }
    break;
  default:
    if (strcmp(str, fmtname(path)) == 0) {
      printf("%s\n", path);
    }
    break;
  }
  close(fd);
}

int main(int argc, char *argv[])
{
  if (argc != 3) {
    printf("find: need two parameters.\n");
    exit(1);
  }

  findStrFromPath(argv[1], argv[2]);
  exit(0);
}
