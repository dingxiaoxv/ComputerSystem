# Chapter12 §12.1 实验构建

CC := gcc
ROOT := ../../..
RIO_DIR := $(ROOT)/Chapter10/rio
NET_DIR := $(ROOT)/Chapter11/socket
CFLAGS := -g -O1 -Wall -I$(RIO_DIR) -I$(NET_DIR)
LDLIBS := -pthread -lrt

RIO_SRC := $(RIO_DIR)/rio.c
NET_H := $(NET_DIR)/net.h

all: process_echo_server process_echo_client ipc_demo

process_echo_server: process_echo_server.c $(RIO_SRC) $(RIO_DIR)/rio.h $(NET_H)
	$(CC) $(CFLAGS) -o $@ process_echo_server.c $(RIO_SRC)

process_echo_client: process_echo_client.c $(RIO_SRC) $(RIO_DIR)/rio.h $(NET_H)
	$(CC) $(CFLAGS) -o $@ process_echo_client.c $(RIO_SRC)

ipc_demo: ipc_demo.c
	$(CC) $(CFLAGS) -o $@ ipc_demo.c $(LDLIBS)

# 一键跑 IPC 原语最小例程
ipc: ipc_demo
	./ipc_demo all

# 一键自测并发 echo：后台起 server，两个 client 依次连入，再清理 server
process-echo: process_echo_server process_echo_client
	@./process_echo_server 8080 > .process_echo_server.log 2>&1 & echo $$! > .process_echo_server.pid
	@sleep 0.3
	@printf 'hello from client A\n' | ./process_echo_client 127.0.0.1 8080
	@printf 'hello from client B\n' | ./process_echo_client 127.0.0.1 8080
	@kill `cat .process_echo_server.pid` 2>/dev/null || true
	@rm -f .process_echo_server.pid
	@echo '[demo] server log:'
	@cat .process_echo_server.log

clean:
	rm -f process_echo_server process_echo_client ipc_demo \
	      .process_echo_server.pid .process_echo_server.log \
	      /tmp/csapp_ipc_demo.sock

.PHONY: all ipc process-echo clean
