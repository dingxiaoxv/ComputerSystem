#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {
  // int fd1, fd2;
  // char c;
  // fd1 = open("foobar.txt", O_RDONLY, 0);
  // fd2 = open("foobar.txt", O_RDONLY, 0);
  // read(fd1, &c, 1);
  // printf("fd1 c=%c\n", c);
  // read(fd2, &c, 1);
  // printf("fd2 c=%c\n", c);

  int fd;
  char c;
  fd = open("foobar.txt", O_RDONLY, 0);
  if (fork() == 0) {
    read(fd, &c, 1);
    printf("son c=%c\n", c);
    exit(0);
  }

  wait(NULL);
  read(fd, &c, 1);
  printf("parent c=%c\n", c);
  exit(0);
}