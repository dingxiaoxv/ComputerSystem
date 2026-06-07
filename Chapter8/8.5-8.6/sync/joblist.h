#ifndef JOBLIST_H
#define JOBLIST_H

#include <sys/types.h>

#define MAXJOBS 16

/* 极简 job 列表：模拟 shell 维护的后台作业表。
   addjob 在主控制流（fork 之后）调用，deletejob 在 SIGCHLD 处理程序里调用，
   两者并发访问同一份全局数组——这正是 §8.5.6 竞态的来源。 */

void initjobs(void);
void addjob(pid_t pid);    /* 主流程用：可以用 printf */
void deletejob(pid_t pid); /* 信号处理程序用：只能用 sio_* */
int jobs_remaining(void);  /* 主流程用：还剩多少个 job 未回收 */

#endif /* JOBLIST_H */
