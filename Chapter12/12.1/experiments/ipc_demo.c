#define _GNU_SOURCE

#include <errno.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define MSG_SIZE 128

struct shared_region {
  sem_t ready;
  char message[MSG_SIZE];
};

static void die(const char *msg) {
  perror(msg);
  exit(1);
}

static void write_all(int fd, const void *buf, size_t n) {
  const char *p = buf;
  size_t left = n;

  while (left > 0) {
    ssize_t written = write(fd, p, left);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      die("write");
    }
    left -= (size_t)written;
    p += written;
  }
}

static void wait_child(pid_t pid) {
  int status;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR) {
      continue;
    }
    die("waitpid");
  }

  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr, "child %ld exited abnormally\n", (long)pid);
    exit(1);
  }
}

static void demo_uds(void) {
  const char *path = "/tmp/csapp_ipc_demo.sock";
  unlink(path);

  int listenfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listenfd < 0) {
    die("socket(AF_UNIX)");
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

  if (bind(listenfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    die("bind(AF_UNIX)");
  }
  if (listen(listenfd, 8) < 0) {
    die("listen(AF_UNIX)");
  }

  pid_t pid = fork();
  if (pid < 0) {
    die("fork");
  }
  if (pid == 0) {
    int clientfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (clientfd < 0) {
      die("socket client(AF_UNIX)");
    }
    if (connect(clientfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
      die("connect(AF_UNIX)");
    }

    write_all(clientfd, "hello via UDS", strlen("hello via UDS"));

    char reply[MSG_SIZE];
    ssize_t n = read(clientfd, reply, sizeof(reply) - 1);
    if (n < 0) {
      die("read UDS reply");
    }
    reply[n] = '\0';
    dprintf(STDOUT_FILENO, "uds child read reply: %s\n", reply);
    close(clientfd);
    _exit(0);
  }

  int connfd = accept(listenfd, NULL, NULL);
  if (connfd < 0) {
    die("accept(AF_UNIX)");
  }

  char buf[MSG_SIZE];
  ssize_t n = read(connfd, buf, sizeof(buf) - 1);
  if (n < 0) {
    die("read(AF_UNIX)");
  }
  buf[n] = '\0';
  printf("uds parent read request: %s\n", buf);
  write_all(connfd, "reply from UDS server", strlen("reply from UDS server"));

  close(connfd);
  close(listenfd);
  wait_child(pid);
  unlink(path);
}

static void demo_mmap(void) {
  struct shared_region *region = mmap(NULL, sizeof(*region), PROT_READ | PROT_WRITE,
                                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (region == MAP_FAILED) {
    die("mmap");
  }

  if (sem_init(&region->ready, 1, 0) < 0) {
    die("sem_init");
  }

  pid_t pid = fork();
  if (pid < 0) {
    die("fork");
  }
  if (pid == 0) {
    while (sem_wait(&region->ready) < 0) {
      if (errno == EINTR) {
        continue;
      }
      die("sem_wait");
    }
    dprintf(STDOUT_FILENO, "mmap child read shared data: %s\n", region->message);
    munmap(region, sizeof(*region));
    _exit(0);
  }

  snprintf(region->message, sizeof(region->message), "hello via MAP_SHARED mmap");
  if (sem_post(&region->ready) < 0) {
    die("sem_post");
  }

  wait_child(pid);
  sem_destroy(&region->ready);
  munmap(region, sizeof(*region));
}

static void demo_eventfd(void) {
  int efd = eventfd(0, 0);
  if (efd < 0) {
    die("eventfd");
  }

  pid_t pid = fork();
  if (pid < 0) {
    die("fork");
  }
  if (pid == 0) {
    uint64_t value;
    ssize_t n = read(efd, &value, sizeof(value));
    if (n != (ssize_t)sizeof(value)) {
      die("read(eventfd)");
    }
    dprintf(STDOUT_FILENO, "eventfd child read counter: %llu\n",
            (unsigned long long)value);
    close(efd);
    _exit(0);
  }

  uint64_t value = 3;
  write_all(efd, &value, sizeof(value));
  wait_child(pid);
  close(efd);
}

static void usage(const char *prog) {
  fprintf(stderr, "usage: %s <all|uds|mmap|eventfd>\n", prog);
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);

  if (argc != 2) {
    usage(argv[0]);
    return 1;
  }

  if (strcmp(argv[1], "uds") == 0) {
    demo_uds();
  } else if (strcmp(argv[1], "mmap") == 0) {
    demo_mmap();
  } else if (strcmp(argv[1], "eventfd") == 0) {
    demo_eventfd();
  } else if (strcmp(argv[1], "all") == 0) {
    demo_uds();
    demo_mmap();
    demo_eventfd();
  } else {
    usage(argv[0]);
    return 1;
  }

  return 0;
}
