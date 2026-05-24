#include <stddef.h>
#include <stdio.h>

struct A {
  char a;
  int b;
  char c;
  short d;
};
struct B {
  int b;
  short d;
  char a;
  char c;
};
struct C {
  char a;
  char c;
  short d;
  int b;
};

#define DUMP(S)                                                                                    \
  do {                                                                                             \
    printf("struct " #S ": sizeof=%zu alignof=%zu\n", sizeof(struct S), _Alignof(struct S));       \
    printf("  off(a)=%zu off(b)=%zu off(c)=%zu off(d)=%zu\n\n", offsetof(struct S, a),             \
           offsetof(struct S, b), offsetof(struct S, c), offsetof(struct S, d));                   \
  } while (0)

struct Normal {
  char a;
  int b;
};
struct __attribute__((packed)) Packed {
  char a;
  int b;
};

int read_normal(struct Normal *s) { return s->b; }
int read_packed(struct Packed *s) { return s->b; }

int main(void) {
  DUMP(A);
  DUMP(B);
  DUMP(C);
  return 0;
}
