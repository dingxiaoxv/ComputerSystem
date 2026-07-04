# Chapter10 实验构建
# rio.* 在 ../rio 下，实验代码链接它。

RIO_DIR := ../rio
CFLAGS  := -g -O1 -Wall -I$(RIO_DIR)
LDFLAGS := -lpthread

# 共享同一个 rio_t 的竞态实验，需要 ThreadSanitizer
race: tsan_race.c $(RIO_DIR)/rio.c data.txt
	gcc -fsanitize=thread $(CFLAGS) tsan_race.c $(RIO_DIR)/rio.c -o tsan_race $(LDFLAGS)
	setarch -R ./tsan_race

# TSan 实验的输入：两万行文本，行数足够多才能稳定触发竞态
data.txt:
	seq 1 20000 | sed 's/^/line-/' > data.txt

# ---- Unix Domain Socket 本机 IPC 实验 ----
UDS_SOCK := /tmp/csapp_uds.sock

uds_server: uds_server.c $(RIO_DIR)/rio.c
	gcc $(CFLAGS) uds_server.c $(RIO_DIR)/rio.c -o uds_server

uds_client: uds_client.c $(RIO_DIR)/rio.c
	gcc $(CFLAGS) uds_client.c $(RIO_DIR)/rio.c -o uds_client

# 一键自测：后台起 server，用 client 喂两行，比对大写回显，再收尾清理
uds: uds_server uds_client
	@rm -f $(UDS_SOCK)
	@./uds_server $(UDS_SOCK) & echo $$! > .uds_server.pid
	@sleep 0.3
	@printf 'hello\nunix domain socket\n' | ./uds_client $(UDS_SOCK)
	@kill `cat .uds_server.pid` 2>/dev/null; rm -f .uds_server.pid $(UDS_SOCK)
	@echo "[demo] done. 'ls -l $(UDS_SOCK)' 期间可见一个 s 类型文件"

# ---- SCM_RIGHTS 传递 fd 实验（socketpair + fork，无需 bind/connect）----
passfd: uds_passfd.c
	gcc $(CFLAGS) uds_passfd.c -o uds_passfd
	@echo "[demo] 父进程 open 一个文件，把 fd 传给子进程，子进程用新 fd 读出内容："
	@./uds_passfd

# ---- 命名 UDS 版传 fd（两个独立进程，非父子）----
PASSFD_SOCK := /tmp/csapp_passfd.sock

passfd_send: passfd_send.c
	gcc $(CFLAGS) passfd_send.c -o passfd_send

passfd_recv: passfd_recv.c
	gcc $(CFLAGS) passfd_recv.c -o passfd_recv

# 自测：后台起 send（独立进程），再起 recv（另一独立进程）连上来收 fd、读文件。
# 两者无亲缘关系，仅靠命名 UDS 相连。手动玩：两个终端各跑 ./passfd_send 和 ./passfd_recv。
passfd2: passfd_send passfd_recv
	@rm -f $(PASSFD_SOCK)
	@./passfd_send & echo $$! > .passfd_send.pid
	@sleep 0.3
	@./passfd_recv
	@wait `cat .passfd_send.pid` 2>/dev/null; rm -f .passfd_send.pid $(PASSFD_SOCK)
	@echo "[demo] 注意：send 端 fd 号与 recv 端 fd 号不同，却读到同一文件"

# ---- 现代 C++17 RAII 封装版传 fd（unix_socket.hpp）----
CXXFLAGS := -g -O1 -std=c++17 -Wall -Wextra

passfd_cpp: passfd_cpp.cpp unix_socket.hpp
	g++ $(CXXFLAGS) passfd_cpp.cpp -o passfd_cpp
	@echo "[demo] RAII 封装版：父发子收，全程无手动 close/memset："
	@./passfd_cpp

clean:
	rm -f tsan_race data.txt uds_server uds_client uds_passfd \
	      passfd_send passfd_recv passfd_cpp .uds_server.pid .passfd_send.pid \
	      $(UDS_SOCK) $(PASSFD_SOCK) /tmp/csapp_passfd_cpp.sock

.PHONY: race uds passfd passfd2 passfd_cpp clean
