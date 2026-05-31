#define ARRAY_size 2

int sum(int *a, int n);

int array[ARRAY_size] = {1, 2};

int main() {
  int val = sum(array, ARRAY_size);
  return val;
}