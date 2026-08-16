# §9.11 内存错误实验
#
#   make          编译两个版本：mem_errors（普通）和 mem_errors_asan（ASan）
#   make demo     跑一遍不需要交互输入的错误类型，对比两个版本的表现
#   make vg-uninit 用 valgrind 抓 ASan 抓不到的「读未初始化内存」

CC      = gcc
# -O0 保证越界不被优化掉；-g 让 ASan/valgrind 报出行号
CFLAGS  = -O0 -g -Wall -Wextra
ASANFLAGS = -fsanitize=address -fno-omit-frame-pointer

TARGETS = mem_errors mem_errors_asan

.PHONY: all demo vg-uninit clean

all: $(TARGETS)

mem_errors: mem_errors.c
	$(CC) $(CFLAGS) -o $@ $<

mem_errors_asan: mem_errors.c
	$(CC) $(CFLAGS) $(ASANFLAGS) -o $@ $<

# 只跑不需要 stdin 的类型
NOINPUT = uninit ptrsize offbyone deref_ptr ptr_arith dangling_stack use_after_free leak

demo: $(TARGETS)
	@for t in $(NOINPUT); do \
		echo "════════ $$t ════════"; \
		echo "--- 普通编译 ---"; \
		out=$$(./mem_errors $$t 2>&1); rc=$$?; \
		echo "$$out" | head -3; echo "  (退出码 $$rc)"; \
		echo "--- ASan ---"; \
		./mem_errors_asan $$t 2>&1 \
			| grep -E "ERROR|SUMMARY|detected memory leaks|Direct leak" | head -4; \
		echo; \
	done

# 需要 stdin 的两类单独跑
demo-stdin: $(TARGETS)
	@echo "════════ stack_overflow：短输入（安全）════════"
	@echo "abc" | ./mem_errors stack_overflow 2>&1 | tail -1
	@echo "════════ stack_overflow：长输入（触发 canary）════════"
	@printf 'A%.0s' $$(seq 40) | ./mem_errors stack_overflow 2>&1 | tail -1
	@echo "════════ badptr（ASan）════════"
	@echo 123 | ./mem_errors_asan badptr 2>&1 | grep -E "ERROR|SUMMARY" | head -2

# ASan 默认**不检测**「读未初始化内存」，valgrind 可以，还能报出值的来源
vg-uninit: mem_errors
	@echo "--- ASan 对 uninit 完全静默 ---"
	@./mem_errors_asan uninit 2>&1 | head -2
	@echo "--- valgrind --track-origins=yes ---"
	valgrind --track-origins=yes ./mem_errors uninit 2>&1 \
		| grep -E "uninitialised|Uninitialised|ERROR SUMMARY|err_uninit" | head -8

clean:
	rm -f $(TARGETS)
