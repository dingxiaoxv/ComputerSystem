/*
 * tsan_race.c —— 用 ThreadSanitizer 抓 rio_t 的数据竞争。
 *
 * 两个线程共享同一个 rio_t，并发调用 rio_readlineb。
 * rio_readlineb -> rio_read 会读写 rio_t 里的共享状态：
 *     rp->rio_cnt     缓冲剩余字节数
 *     rp->rio_bufptr  下一个待读字节
 *     rp->rio_buf     内部缓冲区
 * 这三个没有任何锁保护，两个线程必然在它们上面竞争。
 *
 * 构建运行： make race          （见本目录 Makefile，自动生成 data.txt）
 * 手动编译： gcc -fsanitize=thread -g -O1 tsan_race.c ../rio/rio.c -I../rio \
 *                -o tsan_race -lpthread
 * 运行：     setarch -R ./tsan_race  （setarch -R 关 ASLR，绕开 TSan 影子内存冲突）
 * 预期：     TSan 报告 data race，栈回溯指向 rio_read 里对 rio_cnt/rio_bufptr 的读写。
 */
#include "rio.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static rio_t shared_rio; /* 故意只用一个，让两个线程抢 */

static void *reader(void *arg) {
  long id = (long)arg;
  char line[256];
  ssize_t n;

  /* 两个线程同时从同一个 rio_t 读，撞 rio_cnt/rio_bufptr */
  while ((n = rio_readlineb(&shared_rio, line, sizeof(line))) > 0) {
    /* 不打印内容，避免 stdout 锁干扰；只消费 */
    (void)id;
  }
  return NULL;
}

int main(void) {
  int fd = open("data.txt", O_RDONLY);
  if (fd < 0) {
    perror("open data.txt");
    return 1;
  }

  rio_initb(&shared_rio, fd);

  pthread_t t1, t2;
  pthread_create(&t1, NULL, reader, (void *)1);
  pthread_create(&t2, NULL, reader, (void *)2);
  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  return 0;
}
