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

clean:
	rm -f tsan_race data.txt uds_server uds_client .uds_server.pid $(UDS_SOCK)

.PHONY: race uds clean
