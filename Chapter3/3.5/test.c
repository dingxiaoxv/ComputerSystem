long mul5(long x) {
  return x * 5;
  // leaq	(%rdi,%rdi,4), %rax
}

long mul15(long x) {
  return x * 15;
  // movq	%rdi, %rax
  // salq	$4, %rax
  // subq	%rdi, %rax
}

long mul6(long x) {
  return x * 6;
  // leaq	(%rdi,%rdi,2), %rax
  // addq	%rax, %rax
}

long signed_shift(long x) {
  return x >> 3;
  // movq	%rdi, %rax
  // sarq	$3, %rax
}

unsigned long unsigned_shift(unsigned long x) {
  return x >> 3;
  // movq	%rdi, %rax
  // shrq	$3, %rax
}

long div_by_4(long x) {
  return x / 4;
  // leaq	3(%rdi), %rax
  // testq	%rdi, %rdi
  // cmovns	%rdi, %rax
  // sarq	$2, %rax
}