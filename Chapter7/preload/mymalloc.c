/* CSAPP §7.13.3 运行时打桩（runtime interpositioning）例程
 *
 * 思路：自己写一个同名 malloc/free，编进 .so，用 LD_PRELOAD 抢在
 * libc 之前被加载。这样目标程序里所有对 malloc/free 的调用都先落到
 * 我们的包装函数里，包装函数再用 dlsym(RTLD_NEXT, ...) 拿到 libc 的
 * 真实实现转发过去——既不改、不重编目标程序，又能在中间插入日志。
 *
 * 编译：
 *   gcc -DRUNTIME -shared -fPIC -o mymalloc.so mymalloc.c -ldl
 * 使用：
 *   LD_PRELOAD=./mymalloc.so ./intr
 */
#ifdef RUNTIME
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

/* ⚠️ 对书本例程的一处必要修正：重入保护。
 * 包装函数里调用 printf，而 printf 内部又会调用 malloc（给 stdio 缓冲区
 * 分配空间），于是 malloc→printf→malloc→… 无限递归，栈溢出段错误。
 * 书上的原版代码在现代 glibc 上直接跑会崩。用一个线程局部标志位挡住
 * "正在打印时再进来的那次调用"，让它直接转发、不再打印即可。 */
static __thread int in_hook = 0;

/* malloc 包装函数 */
void *malloc(size_t size) {
  void *(*mallocp)(size_t size);
  char *error;

  mallocp = dlsym(RTLD_NEXT, "malloc"); /* 取 libc 里真正的 malloc 地址 */
  if ((error = dlerror()) != NULL) {
    fputs(error, stderr);
    exit(1);
  }
  char *ptr = mallocp(size); /* 调用 libc 的 malloc */
  if (!in_hook) {                    /* 只在最外层调用时打印 */
    in_hook = 1;
    printf("malloc(%d) = %p\n", (int)size, ptr);
    in_hook = 0;
  }
  return ptr;
}

/* free 包装函数 */
void free(void *ptr) {
  void (*freep)(void *) = NULL;
  char *error;

  if (!ptr)
    return;

  freep = dlsym(RTLD_NEXT, "free"); /* 取 libc 里真正的 free 地址 */
  if ((error = dlerror()) != NULL) {
    fputs(error, stderr);
    exit(1);
  }
  freep(ptr); /* 调用 libc 的 free */
  if (!in_hook) {
    in_hook = 1;
    printf("free(%p)\n", ptr);
    in_hook = 0;
  }
}
#endif
