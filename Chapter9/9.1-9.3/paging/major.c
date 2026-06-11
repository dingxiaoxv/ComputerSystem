#include <fcntl.h>    /* open, O_RDONLY */
#include <stdio.h>    /* printf */
#include <sys/mman.h> /* mmap, PROT_READ, MAP_PRIVATE */
#include <sys/stat.h> /* fstat, struct stat */

int main() {
  /* mmap 读一个大文件的第一个字节 × 每页 */
  int fd = open("bigfile", O_RDONLY);
  struct stat st;
  fstat(fd, &st);
  char *m = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  long sum = 0;
  for (off_t i = 0; i < st.st_size; i += 4096)
    sum += m[i];
  /* 打印 sum：否则开优化时整个循环会被当死代码删掉，缺页就测不到了 */
  printf("sum = %ld\n", sum);
}