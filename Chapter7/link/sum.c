int sum(int *a, int n) {
  int i = 0, s = 0;
  for (i = 0; i < n; i++) {
    s += a[i];
  }
  return s;
}