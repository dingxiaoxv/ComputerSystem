/*
 * adder.c —— CSAPP 书本展示版 CGI 程序（Figure 11.36）
 *
 * 从环境变量 QUERY_STRING 取两个整数（形如 "15000&213"），求和后
 * 把结果作为 HTML 页面写到标准输出——而标准输出已被 Tiny 用 dup2
 * 重定向到客户端 socket，所以内容会直接发回浏览器。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXLINE 8192

int main(void) {
  char *buf, *p;
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
  int n1 = 0, n2 = 0;

  /* 提取两个参数：QUERY_STRING = "n1&n2" */
  if ((buf = getenv("QUERY_STRING")) != NULL) {
    p = strchr(buf, '&');
    *p = '\0';
    strcpy(arg1, buf);
    strcpy(arg2, p + 1);
    n1 = atoi(arg1);
    n2 = atoi(arg2);
  }

  /* 生成响应体 */
  sprintf(content, "QUERY_STRING=%s", buf ? buf : "");
  sprintf(content + strlen(content), "Welcome to add.com: ");
  sprintf(content + strlen(content), "THE Internet addition portal.\r\n<p>");
  sprintf(content + strlen(content), "The answer is: %d + %d = %d\r\n<p>", n1, n2, n1 + n2);
  sprintf(content + strlen(content), "Thanks for visiting!\r\n");

  /* 生成 HTTP 响应（其余头部由 CGI 程序补齐）*/
  printf("Connection: close\r\n");
  printf("Content-length: %d\r\n", (int)strlen(content));
  printf("Content-type: text/html\r\n\r\n");
  printf("%s", content);
  fflush(stdout);

  exit(0);
}
