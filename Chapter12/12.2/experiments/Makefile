# Chapter12 §12.2 I/O 多路复用实验构建

CC := gcc
ROOT := ../../..
RIO_DIR := $(ROOT)/Chapter10/rio
NET_DIR := $(ROOT)/Chapter11/socket
CLIENT_SRC := ../../12.1/experiments/process_echo_client.c
CFLAGS := -g -O1 -Wall -I$(RIO_DIR) -I$(NET_DIR)

RIO_SRC := $(RIO_DIR)/rio.c
RIO_H := $(RIO_DIR)/rio.h
NET_H := $(NET_DIR)/net.h

SELECT_PORT ?= 18080
POLL_PORT ?= 18081
EPOLL_PORT ?= 18082

all: select_echo_server poll_echo_server epoll_echo_server epoll_et_echo_server echo_client

select_echo_server: select_echo_server.c $(RIO_SRC) $(RIO_H) $(NET_H)
	$(CC) $(CFLAGS) -o $@ select_echo_server.c $(RIO_SRC)

poll_echo_server: poll_echo_server.c $(RIO_SRC) $(RIO_H) $(NET_H)
	$(CC) $(CFLAGS) -o $@ poll_echo_server.c $(RIO_SRC)

epoll_echo_server: epoll_echo_server.c $(RIO_SRC) $(RIO_H) $(NET_H)
	$(CC) $(CFLAGS) -o $@ epoll_echo_server.c $(RIO_SRC)

epoll_et_echo_server: epoll_et_echo_server.c $(NET_H)
	$(CC) $(CFLAGS) -o $@ epoll_et_echo_server.c

echo_client: $(CLIENT_SRC) $(RIO_SRC) $(RIO_H) $(NET_H)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_SRC) $(RIO_SRC)

# 后台起 server，跑两个 client，校验回显内容，再清理 server。
select-echo: select_echo_server echo_client
	@rm -f .select_echo_server.log .select_echo_client.out .select_echo_server.pid
	@./select_echo_server $(SELECT_PORT) > .select_echo_server.log 2>&1 & echo $$! > .select_echo_server.pid
	@sleep 0.3
	@printf 'hello from select A\n' | ./echo_client 127.0.0.1 $(SELECT_PORT) > .select_echo_client.out
	@printf 'hello from select B\n' | ./echo_client 127.0.0.1 $(SELECT_PORT) >> .select_echo_client.out
	@grep -q 'hello from select A' .select_echo_client.out
	@grep -q 'hello from select B' .select_echo_client.out
	@kill `cat .select_echo_server.pid` 2>/dev/null || true
	@rm -f .select_echo_server.pid
	@echo '[ok] select echo'
	@echo '[demo] select server log:'
	@cat .select_echo_server.log

poll-echo: poll_echo_server echo_client
	@rm -f .poll_echo_server.log .poll_echo_client.out .poll_echo_server.pid
	@./poll_echo_server $(POLL_PORT) > .poll_echo_server.log 2>&1 & echo $$! > .poll_echo_server.pid
	@sleep 0.3
	@printf 'hello from poll A\n' | ./echo_client 127.0.0.1 $(POLL_PORT) > .poll_echo_client.out
	@printf 'hello from poll B\n' | ./echo_client 127.0.0.1 $(POLL_PORT) >> .poll_echo_client.out
	@grep -q 'hello from poll A' .poll_echo_client.out
	@grep -q 'hello from poll B' .poll_echo_client.out
	@kill `cat .poll_echo_server.pid` 2>/dev/null || true
	@rm -f .poll_echo_server.pid
	@echo '[ok] poll echo'
	@echo '[demo] poll server log:'
	@cat .poll_echo_server.log

epoll-echo: epoll_echo_server echo_client
	@rm -f .epoll_echo_server.log .epoll_echo_client.out .epoll_echo_server.pid
	@./epoll_echo_server $(EPOLL_PORT) > .epoll_echo_server.log 2>&1 & echo $$! > .epoll_echo_server.pid
	@sleep 0.3
	@printf 'hello from epoll A\n' | ./echo_client 127.0.0.1 $(EPOLL_PORT) > .epoll_echo_client.out
	@printf 'hello from epoll B\n' | ./echo_client 127.0.0.1 $(EPOLL_PORT) >> .epoll_echo_client.out
	@grep -q 'hello from epoll A' .epoll_echo_client.out
	@grep -q 'hello from epoll B' .epoll_echo_client.out
	@kill `cat .epoll_echo_server.pid` 2>/dev/null || true
	@rm -f .epoll_echo_server.pid
	@echo '[ok] epoll echo'
	@echo '[demo] epoll server log:'
	@cat .epoll_echo_server.log

demo: select-echo poll-echo epoll-echo

clean:
	rm -f select_echo_server poll_echo_server epoll_echo_server epoll_et_echo_server echo_client \
	      .select_echo_server.pid .select_echo_server.log .select_echo_client.out \
	      .poll_echo_server.pid .poll_echo_server.log .poll_echo_client.out \
	      .epoll_echo_server.pid .epoll_echo_server.log .epoll_echo_client.out

.PHONY: all select-echo poll-echo epoll-echo demo clean
