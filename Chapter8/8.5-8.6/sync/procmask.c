/* §8.5.6 进程同步 —— 正确的版本（对应书中图 8.38）
 *
 * 修复思路：保证 addjob 一定先于对应的 deletejob 发生。
 *   1. fork 之前阻塞 SIGCHLD —— 这样即使子进程瞬间结束，
 *      SIGCHLD 也会被挂起，handler 不会抢在 addjob 之前运行；
 *   2. addjob 完成之后再解除阻塞 —— 此时挂起的 SIGCHLD 才被投递，
 *      handler 跑 deletejob，列表里一定能找到对应的 job；
 *   3. 子进程必须在 fork 后立即恢复信号掩码 —— 否则它（以及它将来
 *      execve 出的程序）会继承"SIGCHLD 被阻塞"的状态。
 *
 * 即使同样让子进程立即退出，这个版本也绝不会出现 "not found"。
 */

#include "csapp.h"
#include "joblist.h"

void handler(int sig) {
  int olderrno = errno;
  sigset_t mask_all, prev_all;
  pid_t pid;
  (void)sig; /* 本例不关心具体信号编号 */

  try_sigfillset(&mask_all);
  while ((pid = waitpid(-1, NULL, 0)) > 0) { /* 回收每一个僵尸子进程 */
    try_sigprocmask(SIG_BLOCK, &mask_all, &prev_all); /* 访问 joblist 前阻塞所有信号 */
    deletejob(pid);
    try_sigprocmask(SIG_SETMASK, &prev_all, NULL);
  }
  if (errno != ECHILD) {
    sio_error("waitpid error");
  }
  errno = olderrno;
}

int main(void) {
  pid_t pid;
  sigset_t mask_all, mask_one, prev_one;
  char *child_argv[] = {"/bin/true", NULL};

  try_sigfillset(&mask_all);
  try_sigemptyset(&mask_one);
  try_sigaddset(&mask_one, SIGCHLD); /* mask_one 只含 SIGCHLD */

  try_signal(SIGCHLD, handler);
  initjobs();

  for (int i = 0; i < 5; i++) {
    try_sigprocmask(SIG_BLOCK, &mask_one, &prev_one); /* fork 前阻塞 SIGCHLD */

    if ((pid = try_fork()) == 0) {
      try_sigprocmask(SIG_SETMASK, &prev_one, NULL); /* 子进程恢复掩码 */
      try_execve("/bin/true", child_argv, NULL);
    }

    /* 访问共享 joblist 前阻塞所有信号；oldset 传 NULL，
       不要覆盖 prev_one（它保存着 fork 前"SIGCHLD 未阻塞"的掩码） */
    try_sigprocmask(SIG_BLOCK, &mask_all, NULL);
    addjob(pid);
    try_sigprocmask(SIG_SETMASK, &prev_one, NULL); /* 解除阻塞：挂起的 SIGCHLD 此刻才投递 */
  }

  while (jobs_remaining() > 0) { /* 等所有子进程被 handler 回收 */
    sleep(1);
  }
  exit(0);
}
