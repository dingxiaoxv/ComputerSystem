/* §8.5.7 显式等待信号 —— 对应书中图 8.39 的 sigsuspend 正确版
 *
 * 场景：shell 启动前台作业后，必须等这个子进程结束（即等到它的
 * SIGCHLD 被回收）才能继续读下一条命令。父进程怎么"等一个信号"？
 *
 * 四种写法，前三种都有毛病：
 *
 *   ① 忙等待  while (!pid) ;
 *      逻辑对，但空转烧 CPU，完全浪费。
 *
 *   ② pause   while (!pid) pause();
 *      有致命竞态：若 SIGCHLD 在"判断 !pid 为真"之后、"调用 pause"
 *      之前到达，handler 已把 pid 写好，但 pause 仍会陷入永久睡眠
 *      （信号已经过去，不会再来叫醒它）→ 程序卡死。
 *
 *   ③ sleep   while (!pid) sleep(1);
 *      能正确工作，但要么响应慢（睡过头），要么仍然偏忙（睡太短）。
 *
 *   ④ sigsuspend(&prev)  ← 正确解
 *      原子地完成"把信号掩码临时换成 prev（解除对 SIGCHLD 的阻塞）
 *      + pause"，返回时再恢复原掩码。因为解除阻塞和挂起是一个不可
 *      分割的动作，彻底关掉了 ② 的竞态窗口。
 *
 * 关键前提：fork 之前先阻塞 SIGCHLD，这样从 fork 到进入等待之间
 * 子进程即使瞬间结束，信号也只是被挂起、不会丢失。
 */

#include "csapp.h"

volatile sig_atomic_t pid; /* 子进程结束时由 handler 写入其 pid */

void sigchld_handler(int s) {
  int olderrno = errno;
  (void)s;
  pid = waitpid(-1, NULL, 0); /* 回收子进程并记下它的 pid */
  errno = olderrno;
}

void sigint_handler(int s) {
  (void)s; /* 空处理：演示 sigsuspend 也会被 SIGINT 唤醒，
              但因为 pid 没被改写，等待循环会继续挂起 */
}

int main(void) {
  sigset_t mask, prev;

  try_signal(SIGCHLD, sigchld_handler);
  try_signal(SIGINT, sigint_handler);

  try_sigemptyset(&mask);
  try_sigaddset(&mask, SIGCHLD); /* mask 只含 SIGCHLD */

  for (int i = 0; i < 5; i++) {
    try_sigprocmask(SIG_BLOCK, &mask, &prev); /* fork 前阻塞 SIGCHLD */

    if (try_fork() == 0) { /* 子进程：立即退出 */
      /* 必须用 _exit：exit 会刷新继承自父进程的 stdout 缓冲区，
         在输出被重定向（全缓冲）时会重复打印父进程尚未刷新的内容 */
      _exit(0);
    }

    /* 父进程：原子地"解除 SIGCHLD 阻塞 + 挂起"，直到 handler 写入 pid。
       prev 里不含 SIGCHLD，所以 sigsuspend 期间它可以被投递。 */
    pid = 0;
    while (!pid) {
      sigsuspend(&prev);
    }

    try_sigprocmask(SIG_SETMASK, &prev, NULL); /* 恢复原掩码 */

    printf("第 %d 个子进程 pid %d 已回收\n", i + 1, (int)pid);
  }

  exit(0);
}
