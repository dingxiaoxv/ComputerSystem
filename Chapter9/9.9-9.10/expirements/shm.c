/* POSIX 共享内存：父子进程通过 mmap(MAP_SHARED) 通信 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
  /* MAP_SHARED|MAP_ANONYMOUS：父子共享、写互见、不关联文件 */
  char *shm = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  if (fork() == 0) { /* 子进程写 */
    strcpy(shm, "hello from child");
    _exit(0);
  }
  wait(NULL);                      /* 等子进程写完 */
  printf("父进程读到：%s\n", shm); /* 直接读到子进程写的内容，无拷贝 */
  munmap(shm, 4096);
  return 0;
}