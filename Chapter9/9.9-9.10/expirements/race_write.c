/* 并发写同一文件：O_APPEND 与否的差别一目了然
   用法：./append_race 0 → 不带 O_APPEND（行数 < 2N，部分被覆盖）
         ./append_race 1 → 带 O_APPEND（行数 == 2N，无丢失） */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define N 50000

static void writer(int use_append, char tag) {
  int flags = O_WRONLY | O_CREAT | (use_append ? O_APPEND : 0);
  int fd = open("race.log", flags, 0644); /* 各进程独立 open → 独立 offset */
  char line[8];
  int len = snprintf(line, sizeof line, "%c line\n", tag);
  for (int i = 0; i < N; i++)
    write(fd, line, len); /* 每次 write 7 字节，原子，不会撕裂一行 */
  close(fd);
}

int main(int argc, char **argv) {
  int use_append = (argc > 1 && argv[1][0] == '1');
  close(open("race.log", O_WRONLY | O_CREAT | O_TRUNC, 0644)); /* 先清空 */

  if (fork() == 0) {
    writer(use_append, 'B');
    _exit(0);
  }                        /* 子进程写 B */
  writer(use_append, 'A'); /* 父进程写 A */
  wait(NULL);

  printf("O_APPEND=%d，期望行数=%d\n", use_append, 2 * N);
  return 0;
}