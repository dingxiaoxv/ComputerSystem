#include "joblist.h"
#include "csapp.h"

static volatile pid_t jobs[MAXJOBS];
static volatile int njobs;

void initjobs(void) {
  njobs = 0;
  for (int i = 0; i < MAXJOBS; i++) {
    jobs[i] = 0;
  }
}

int jobs_remaining(void) { return njobs; }

/* 在主控制流里调用，所以可以放心用 printf */
void addjob(pid_t pid) {
  if (njobs >= MAXJOBS) {
    return;
  }
  jobs[njobs++] = pid;
  printf("  addjob:    pid %d  (njobs=%d)\n", (int)pid, njobs);
}

/* 在 SIGCHLD 处理程序里调用，只能用异步信号安全的 sio_* 函数 */
void deletejob(pid_t pid) {
  for (int i = 0; i < njobs; i++) {
    if (jobs[i] == pid) {
      jobs[i] = jobs[--njobs]; /* 用末尾元素填补空位 */
      sio_puts("  deletejob: pid ");
      sio_putl(pid);
      sio_puts("\n");
      return;
    }
  }
  /* 走到这里说明要删的 job 根本没被 addjob 过——竞态的可见症状 */
  sio_puts("  *** BUG: deletejob pid ");
  sio_putl(pid);
  sio_puts(" not found (race condition!)\n");
}
