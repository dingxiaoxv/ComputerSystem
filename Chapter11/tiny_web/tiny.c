/*
 * tiny.c —— CSAPP 书本展示版 Tiny Web 服务器（Figure 11.29-11.35）
 *
 * 迭代式（串行）HTTP/1.0 服务器，支持：
 *   - 静态内容：把磁盘文件 mmap 后原样发回（HTML/图片/文本）
 *   - 动态内容：fork + execve 运行 CGI 程序，用 QUERY_STRING 传参
 *
 * 复用本仓库已有封装：
 *   ../socket/net.h        open_serverfd（getaddrinfo + bind + listen）
 *   ../../Chapter10/rio    健壮 I/O（按行读 HTTP 请求，短计数安全写）
 *
 * 用法：./tiny <port>，然后浏览器访问
 *   http://localhost:<port>/home.html            静态
 *   http://localhost:<port>/cgi-bin/adder?15000&213  动态
 */
#define _GNU_SOURCE /* 为了用 memmem() 在二进制 body 里找 boundary */
#include "net.h"
#include "rio.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define MAXLINE 8192
#define MAXBUF 8192

#define UPLOAD_DIR "./uploads"           /* 上传文件统一落这里 */
#define MAX_UPLOAD (64 * 1024 * 1024)    /* POST 缓冲整包，限个上限防 OOM */

/* CGI 子进程要继承当前环境变量表 */
extern char **environ;

void doit(int fd);
void read_requesthdrs(rio_t *rp, long *content_length, char *content_type);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void serve_put(int fd, char *uri, long content_length, rio_t *rp);
void serve_post(int fd, char *uri, long content_length, char *content_type, rio_t *rp);
void upload_path(const char *name, char *out);
void upload_response(int fd, const char *status, const char *savepath, long nbytes);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  int listenfd = open_serverfd(argv[1]);
  if (listenfd < 0) {
    fprintf(stderr, "open_serverfd failed\n");
    exit(1);
  }

  mkdir(UPLOAD_DIR, 0755); /* 已存在则失败，无所谓 */

  /* 客户端在我们写响应途中崩溃/断开 -> 内核发 SIGPIPE，默认会杀掉整个 server。
     忽略它，改由 rio_writen 的 write 返回 EPIPE 来感知，单个坏客户端不再拖垮进程 */
  signal(SIGPIPE, SIG_IGN);

  while (1) {
    struct sockaddr_storage clientaddr; /* 协议无关，够装 IPv4/IPv6 */
    socklen_t clientlen = sizeof(clientaddr);
    int connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
    if (connfd < 0) {
      continue;
    }

    char host[256], serv[32];
    getnameinfo((struct sockaddr *)&clientaddr, clientlen, host, sizeof(host), serv,
                sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
    fprintf(stdout, "accepted connection from %s:%s\n", host, serv);

    doit(connfd);  /* 串行处理一整条事务 */
    close(connfd);
  }

  return 0;
}

/*
 * doit - 处理一个 HTTP 事务
 */
void doit(int fd) {
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* 读并解析请求行 */
  rio_initb(&rio, fd);
  if (!rio_readlineb(&rio, buf, MAXLINE)) /* 空请求，直接返回 */
    return;
  fprintf(stdout, "%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  /* 读请求头，顺手抽出上传要用的 Content-Length / Content-Type；
     body（若有）留在 rio 缓冲里，紧跟在头部空行之后 */
  long content_length = -1;
  char content_type[MAXLINE] = "";
  read_requesthdrs(&rio, &content_length, content_type);

  /* PUT / POST：接收上传 */
  if (!strcasecmp(method, "PUT")) {
    serve_put(fd, uri, content_length, &rio);
    return;
  }
  if (!strcasecmp(method, "POST")) {
    serve_post(fd, uri, content_length, content_type, &rio);
    return;
  }
  if (strcasecmp(method, "GET")) { /* 其余方法未实现 */
    clienterror(fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
    return;
  }

  /* 从 URI 解析出文件名与 CGI 参数，判断静态还是动态 */
  int is_static = parse_uri(uri, filename, cgiargs);
  struct stat sbuf;
  if (stat(filename, &sbuf) < 0) { /* 文件不存在 */
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  if (is_static) { /* 提供静态内容 */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size);
  } else { /* 提供动态内容 */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs);
  }
}

/*
 * read_requesthdrs - 读到空行（\r\n）为止；顺路解析出 Content-Length 和
 *                    Content-Type（上传要用）。其余头照旧打印后丢弃。
 *                    调用方需把 *content_length 预置为 -1、content_type 置空。
 */
void read_requesthdrs(rio_t *rp, long *content_length, char *content_type) {
  char buf[MAXLINE];

  rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) {
    fprintf(stdout, "%s", buf);
    if (!strncasecmp(buf, "Content-Length:", 15)) {
      *content_length = atol(buf + 15);
    } else if (!strncasecmp(buf, "Content-Type:", 13)) {
      char *p = buf + 13;
      while (*p == ' ')
        p++;
      strncpy(content_type, p, MAXLINE - 1);
      content_type[MAXLINE - 1] = '\0';
      content_type[strcspn(content_type, "\r\n")] = '\0'; /* 去尾部 \r\n */
    }
    rio_readlineb(rp, buf, MAXLINE);
  }
}

/*
 * parse_uri - 把 URI 拆成文件名和（动态请求的）CGI 参数
 *             返回 1 表示静态内容，0 表示动态内容
 *
 * 约定：URI 里含 "cgi-bin" 即视为动态请求
 */
int parse_uri(char *uri, char *filename, char *cgiargs) {
  if (!strstr(uri, "cgi-bin")) { /* 静态内容 */
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri); /* "/home.html" -> "./home.html" */
    if (uri[strlen(uri) - 1] == '/')
      strcat(filename, "home.html"); /* 目录请求默认首页 */
    return 1;
  } else { /* 动态内容 */
    char *ptr = index(uri, '?');
    if (ptr) {
      strcpy(cgiargs, ptr + 1); /* '?' 之后是参数 */
      *ptr = '\0';
    } else {
      strcpy(cgiargs, "");
    }
    strcpy(filename, ".");
    strcat(filename, uri); /* "/cgi-bin/adder" -> "./cgi-bin/adder" */
    return 0;
  }
}

/*
 * serve_static - 发响应头，再用 mmap 把整个文件映射进内存后一次性写给客户端
 */
void serve_static(int fd, char *filename, int filesize) {
  char filetype[MAXLINE], buf[MAXBUF];

  /* 发送响应行与响应头 */
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf + strlen(buf), "Server: Tiny Web Server\r\n");
  sprintf(buf + strlen(buf), "Connection: close\r\n");
  sprintf(buf + strlen(buf), "Content-length: %d\r\n", filesize);
  sprintf(buf + strlen(buf), "Content-type: %s\r\n\r\n", filetype);
  rio_writen(fd, buf, strlen(buf));
  fprintf(stdout, "Response headers:\n%s", buf);

  /* 发送响应体：mmap 只读私有映射，避免把整文件读进用户缓冲区 */
  int srcfd = open(filename, O_RDONLY, 0);
  char *srcp = mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  close(srcfd);
  rio_writen(fd, srcp, filesize);
  munmap(srcp, filesize);
}

/*
 * get_filetype - 由文件名后缀推断 MIME 类型
 */
void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else
    strcpy(filetype, "text/plain");
}

/*
 * serve_dynamic - fork 子进程运行 CGI 程序，父进程回收
 */
void serve_dynamic(int fd, char *filename, char *cgiargs) {
  char buf[MAXLINE], *emptylist[] = {NULL};

  /* 先返回响应行和服务器头（CGI 程序负责补齐其余头部）*/
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  rio_writen(fd, buf, strlen(buf));

  if (fork() == 0) { /* 子进程 */
    setenv("QUERY_STRING", cgiargs, 1);   /* 把参数放进环境变量 */
    dup2(fd, STDOUT_FILENO);              /* CGI 的标准输出重定向到客户端 socket */
    execve(filename, emptylist, environ); /* 运行 CGI 程序 */
  }
  wait(NULL); /* 父进程阻塞回收子进程，避免僵尸 */
}

/*
 * upload_path - 取 name 的最后一段（basename）拼到 UPLOAD_DIR 下。
 *               只取 basename，天然挡掉 "../" 目录穿越——这正是 parse_uri
 *               缺的那道防线。名字为空或是 "." / ".." 时兜底成 upload.bin。
 */
void upload_path(const char *name, char *out) {
  const char *base = strrchr(name, '/');
  base = base ? base + 1 : name;
  if (*base == '\0' || !strcmp(base, ".") || !strcmp(base, ".."))
    base = "upload.bin";
  snprintf(out, MAXLINE, "%s/%s", UPLOAD_DIR, base);
}

/*
 * upload_response - 上传成功后回一个简单 HTML 页，告知存到哪、多少字节
 */
void upload_response(int fd, const char *status, const char *savepath, long nbytes) {
  char body[MAXBUF], buf[MAXBUF];

  sprintf(body, "<html><title>Upload</title><body bgcolor=\"ffffff\">\r\n");
  sprintf(body + strlen(body), "<p>Saved <b>%s</b> (%ld bytes)\r\n", savepath, nbytes);
  sprintf(body + strlen(body), "<hr><em>The Tiny Web server</em>\r\n</body></html>\r\n");

  sprintf(buf, "HTTP/1.0 %s\r\n", status);
  sprintf(buf + strlen(buf), "Server: Tiny Web Server\r\n");
  sprintf(buf + strlen(buf), "Connection: close\r\n");
  sprintf(buf + strlen(buf), "Content-length: %d\r\n", (int)strlen(body));
  sprintf(buf + strlen(buf), "Content-type: text/html\r\n\r\n");
  rio_writen(fd, buf, strlen(buf));
  rio_writen(fd, body, strlen(body));
}

/*
 * serve_put - PUT 上传：body 就是整个文件内容，长度由 Content-Length 给出。
 *             流式地把 body 从 socket 搬到文件，不整包缓冲，能收超大文件。
 *             URI 的最后一段作文件名，存到 UPLOAD_DIR。成功回 201 Created。
 */
void serve_put(int fd, char *uri, long content_length, rio_t *rp) {
  if (content_length < 0) {
    clienterror(fd, "PUT", "411", "Length Required",
                "PUT requires a Content-Length header");
    return;
  }

  char savepath[MAXLINE];
  upload_path(uri, savepath);

  int outfd = open(savepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (outfd < 0) {
    clienterror(fd, savepath, "500", "Internal Server Error",
                "Tiny couldn't open the file for writing");
    return;
  }

  /* 循环搬运：一次 rio_readnb 未必读满，短计数由 RIO 兜住 */
  char buf[MAXBUF];
  long remaining = content_length;
  while (remaining > 0) {
    size_t want = remaining < (long)sizeof(buf) ? (size_t)remaining : sizeof(buf);
    ssize_t n = rio_readnb(rp, buf, want);
    if (n <= 0)
      break;
    rio_writen(outfd, buf, n);
    remaining -= n;
  }
  close(outfd);

  long saved = content_length - remaining;
  fprintf(stdout, "PUT saved %s (%ld bytes)\n", savepath, saved);
  upload_response(fd, "201 Created", savepath, saved);
}

/*
 * serve_post - POST 上传。两种 body 编码都支持：
 *   1) multipart/form-data（网页表单）：解析 boundary，抠出第一个文件 part 的
 *      filename 和二进制内容。因为文件可能含 '\0'，全程用 memmem/memchr，
 *      绝不用 str* 函数。
 *   2) 其它（如 application/octet-stream）：整个 body 即文件，用 URI 尾段命名。
 *
 * 教学取舍：把整包读进内存后再解析（multipart 边界要回看），故限 MAX_UPLOAD。
 */
void serve_post(int fd, char *uri, long content_length, char *content_type, rio_t *rp) {
  if (content_length < 0) {
    clienterror(fd, "POST", "411", "Length Required",
                "POST requires a Content-Length header");
    return;
  }
  if (content_length > MAX_UPLOAD) {
    clienterror(fd, "POST", "413", "Payload Too Large",
                "Tiny caps POST uploads (see MAX_UPLOAD)");
    return;
  }

  char *body = malloc(content_length);
  if (!body) {
    clienterror(fd, "POST", "500", "Internal Server Error", "out of memory");
    return;
  }
  long got = 0;
  while (got < content_length) {
    ssize_t n = rio_readnb(rp, body + got, content_length - got);
    if (n <= 0)
      break;
    got += n;
  }

  char *content = body; /* 默认整包即文件内容 */
  long content_len = got;
  char filename[MAXLINE] = "";

  char *bpos = strstr(content_type, "boundary=");
  if (bpos) { /* multipart/form-data 分支 */
    char boundary[MAXLINE];
    snprintf(boundary, sizeof(boundary), "--%s", bpos + 9); /* 实际分隔符是 --<boundary> */
    size_t blen = strlen(boundary);

    char *p = memmem(body, got, boundary, blen); /* 第一个分隔符 */
    if (p) {
      p += blen;
      char *hdr_end = memmem(p, body + got - p, "\r\n\r\n", 4); /* part 头结束 */
      if (hdr_end) {
        char *fn = memmem(p, hdr_end - p, "filename=\"", 10); /* 抠 filename */
        if (fn) {
          fn += 10;
          char *fe = memchr(fn, '"', hdr_end - fn);
          if (fe && (size_t)(fe - fn) < sizeof(filename)) {
            memcpy(filename, fn, fe - fn);
            filename[fe - fn] = '\0';
          }
        }
        char *cstart = hdr_end + 4;                            /* 内容起点 */
        char sep[MAXLINE];
        snprintf(sep, sizeof(sep), "\r\n%s", boundary);        /* 内容止于 \r\n--<boundary> */
        char *cend = memmem(cstart, body + got - cstart, sep, strlen(sep));
        if (cend) {
          content = cstart;
          content_len = cend - cstart;
        }
      }
    }
  }

  char savepath[MAXLINE];
  upload_path(filename[0] ? filename : uri, savepath);

  int outfd = open(savepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (outfd < 0) {
    free(body);
    clienterror(fd, savepath, "500", "Internal Server Error",
                "Tiny couldn't open the file for writing");
    return;
  }
  rio_writen(outfd, content, content_len);
  close(outfd);
  free(body);

  fprintf(stdout, "POST saved %s (%ld bytes)\n", savepath, content_len);
  upload_response(fd, "200 OK", savepath, content_len);
}

/*
 * clienterror - 向客户端返回一个 HTML 错误页
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* 构造 HTML 响应体 */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body + strlen(body), "<body bgcolor=\"ffffff\">\r\n");
  sprintf(body + strlen(body), "%s: %s\r\n", errnum, shortmsg);
  sprintf(body + strlen(body), "<p>%s: %s\r\n", longmsg, cause);
  sprintf(body + strlen(body), "<hr><em>The Tiny Web server</em>\r\n");

  /* 发送响应行与响应头 */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  rio_writen(fd, buf, strlen(buf));
  rio_writen(fd, body, strlen(body));
}
