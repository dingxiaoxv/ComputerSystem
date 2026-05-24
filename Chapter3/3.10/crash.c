#include <stdlib.h>
#include <string.h>
struct Node {
  int id;
  char name[16];
  struct Node *next;
};

static void walk(struct Node *n) {
  while (n) {
    strcpy(n->name, "X");
    n = n->next;
  } // 故意不判 n 是否合法
}

int main(void) {
  struct Node *bad = (struct Node *)0xdeadbeef; // 野指针
  struct Node head = {1, "head", bad};
  walk(&head);
  return 0;
}