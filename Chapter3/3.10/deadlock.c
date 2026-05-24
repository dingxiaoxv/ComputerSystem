#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t A = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t B = PTHREAD_MUTEX_INITIALIZER;

static void *t1(void *_) {
  pthread_mutex_lock(&A);
  sleep(1);
  pthread_mutex_lock(&B); // 卡在这
  pthread_mutex_unlock(&B);
  pthread_mutex_unlock(&A);
  return 0;
}

static void *t2(void *_) {
  pthread_mutex_lock(&B);
  sleep(1);
  pthread_mutex_lock(&A); // 卡在这
  pthread_mutex_unlock(&A);
  pthread_mutex_unlock(&B);
  return 0;
}

int main(void) {
  pthread_t a, b;
  pthread_create(&a, 0, t1, 0);
  pthread_create(&b, 0, t2, 0);
  pthread_join(a, 0);
  pthread_join(b, 0);
  return 0;
}