# §3.11 浮点汇编实验
#
#   make asm        生成 -Og 汇编（summary.md 里的片段都出自这里）
#   make avx        对比 SSE2 与 AVX(VEX) 编码
#   make rodata     dump 浮点常数池
#   make negzero    实测 x<0?-x:x 与 fabs 对 -0.0 的行为差异
#   make vararg     验证变参 %al 约定 + xmm 全是 caller-saved

CC     = gcc
CFLAGS = -Wall -Wextra

.PHONY: all asm avx rodata negzero vararg clean

all: asm

asm: fp_asm.c
	$(CC) $(CFLAGS) -Og -S fp_asm.c -o fp_Og.s
	$(CC) $(CFLAGS) -O2 -S fp_asm.c -o fp_O2.s
	@echo "→ fp_Og.s / fp_O2.s 已生成"

# SSE2（默认基线）vs AVX2（VEX 三操作数）
avx: fp_asm.c
	@echo "═══ 默认基线：两操作数，破坏性 ═══"
	@$(CC) -O2 -c fp_asm.c -o /tmp/sse.o && objdump -d /tmp/sse.o | awk '/<fp_add>:/,/ret/'
	@echo "═══ -mavx2：VEX 三操作数，非破坏性 ═══"
	@$(CC) -O2 -mavx2 -c fp_asm.c -o /tmp/avx.o && objdump -d /tmp/avx.o | awk '/<fp_add>:/,/ret/'
	@rm -f /tmp/sse.o /tmp/avx.o

# 浮点常数只能从 .rodata 加载，没有立即数形式
rodata: fp_asm.c
	@$(CC) -O2 -S fp_asm.c -o /tmp/fp.s
	@echo "═══ 常数池（每个标号后取 2 条 .long，小端拼成一个 double）═══"
	@awk '/^\.LC[0-9]+:/{lbl=$$0; c=2; next} \
	      c>0 && /^\t\.long/{print lbl, $$2; c--; next} \
	      c>0 && !/^\t\.align/{c=0}' /tmp/fp.s
	@rm -f /tmp/fp.s

# -0.0 陷阱：手写三目 ≠ fabs
negzero: fp_asm.c negzero.c
	$(CC) $(CFLAGS) -O2 negzero.c fp_asm.c -o negzero
	./negzero

# 变参 %al 约定 + 没有 callee-saved 的 xmm
vararg: vararg.c
	@$(CC) $(CFLAGS) -O2 -S vararg.c -o /tmp/vararg.s
	@echo "═══ printf 前的 %al = 使用的向量寄存器个数 ═══"
	@awk '/^p1:/,/jmp/' /tmp/vararg.s | grep -vE '^\s*\.'
	@awk '/^p2:/,/jmp/' /tmp/vararg.s | grep -vE '^\s*\.'
	@echo "═══ xmm 全是 caller-saved，跨调用只能溢出到栈 ═══"
	@awk '/^keep:/,/ret/' /tmp/vararg.s | grep -vE '^\s*\.'
	@rm -f /tmp/vararg.s

clean:
	rm -f fp_Og.s fp_O2.s fp_asm.s fp_asm.o negzero
