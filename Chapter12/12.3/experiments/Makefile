# Chapter12 §12.3 基于线程的并发服务器实验构建

CC := gcc
CXX := g++
ROOT := ../../..
RIO_DIR := $(ROOT)/Chapter10/rio
NET_DIR := $(ROOT)/Chapter11/socket
CLIENT_SRC := ../../12.1/experiments/process_echo_client.c
CFLAGS := -std=gnu11 -g -O1 -Wall -Wextra -Wpedantic -pthread \
          -I$(RIO_DIR) -I$(NET_DIR)
CXXFLAGS := -std=c++20 -g -O1 -Wall -Wextra -Wpedantic -pthread
# 既有 rio.c 的计数器使用 int，与 size_t 比较会触发历史 signedness 警告；
# 只在编译该复用模块时局部抑制，新实验源码仍接受完整警告检查。
RIO_CFLAGS := $(CFLAGS) -Wno-sign-compare

RIO_SRC := $(RIO_DIR)/rio.c
RIO_H := $(RIO_DIR)/rio.h
NET_H := $(NET_DIR)/net.h

THREAD_PORT ?= 18083

all: thread_echo_server echo_client cpp20_coroutine_scheduler

thread_echo_server: thread_echo_server.o rio.o
	$(CC) $(CFLAGS) -o $@ $^

thread_echo_server.o: thread_echo_server.c $(RIO_H) $(NET_H)
	$(CC) $(CFLAGS) -c -o $@ $<

echo_client: process_echo_client.o rio.o
	$(CC) $(CFLAGS) -o $@ $^

process_echo_client.o: $(CLIENT_SRC) $(RIO_H) $(NET_H)
	$(CC) $(CFLAGS) -c -o $@ $<

rio.o: $(RIO_SRC) $(RIO_H)
	$(CC) $(RIO_CFLAGS) -c -o $@ $<

cpp20_coroutine_scheduler: cpp20_coroutine_scheduler.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

# 先让一条连接保持空闲，再验证其他连接仍能由各自的 worker 及时回显。
thread-echo: thread_echo_server echo_client
	@set -eu; \
	rm -f .thread_echo_server.log .thread_echo_server.pid \
	      .thread_echo_idle.out .thread_echo_fast.out; \
	./thread_echo_server $(THREAD_PORT) > .thread_echo_server.log 2>&1 & \
	server_pid=$$!; \
	printf '%s\n' "$$server_pid" > .thread_echo_server.pid; \
	idle_pid=''; \
	cleanup() { \
	  if [ -n "$$idle_pid" ]; then \
	    kill "$$idle_pid" 2>/dev/null || true; \
	    wait "$$idle_pid" 2>/dev/null || true; \
	  fi; \
	  kill "$$server_pid" 2>/dev/null || true; \
	  wait "$$server_pid" 2>/dev/null || true; \
	  rm -f .thread_echo_server.pid; \
	}; \
	trap cleanup EXIT INT TERM; \
	i=0; \
	while ! grep -q 'listening on port' .thread_echo_server.log; do \
	  kill -0 "$$server_pid" 2>/dev/null || { cat .thread_echo_server.log; exit 1; }; \
	  i=$$((i + 1)); \
	  [ "$$i" -lt 50 ] || { echo 'server readiness timeout' >&2; exit 1; }; \
	  sleep 0.1; \
	done; \
	( sleep 2; printf 'hello from idle client\n' ) | \
	  timeout 5 ./echo_client 127.0.0.1 $(THREAD_PORT) > .thread_echo_idle.out & \
	idle_pid=$$!; \
	i=0; \
	while [ "$$(grep -c 'serving fd' .thread_echo_server.log || true)" -lt 1 ]; do \
	  kill -0 "$$idle_pid" 2>/dev/null || { cat .thread_echo_idle.out; exit 1; }; \
	  i=$$((i + 1)); \
	  [ "$$i" -lt 30 ] || { echo 'idle worker startup timeout' >&2; exit 1; }; \
	  sleep 0.1; \
	done; \
	printf 'hello from fast client A\n' | \
	  timeout 2 ./echo_client 127.0.0.1 $(THREAD_PORT) > .thread_echo_fast.out; \
	printf 'hello from fast client B\n' | \
	  timeout 2 ./echo_client 127.0.0.1 $(THREAD_PORT) >> .thread_echo_fast.out; \
	grep -Fxq 'hello from fast client A' .thread_echo_fast.out; \
	grep -Fxq 'hello from fast client B' .thread_echo_fast.out; \
	wait "$$idle_pid"; \
	idle_pid=''; \
	grep -Fxq 'hello from idle client' .thread_echo_idle.out; \
	echo '[ok] idle connection did not block other worker threads'; \
	echo '[demo] thread server log:'; \
	cat .thread_echo_server.log

demo: thread-echo

coroutine-demo: cpp20_coroutine_scheduler
	./cpp20_coroutine_scheduler

clean:
	rm -f thread_echo_server echo_client \
	      cpp20_coroutine_scheduler \
	      thread_echo_server.o process_echo_client.o rio.o \
	      .thread_echo_server.pid .thread_echo_server.log \
	      .thread_echo_idle.out .thread_echo_fast.out

.PHONY: all thread-echo demo coroutine-demo clean
