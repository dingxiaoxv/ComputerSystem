CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -I../socket
CXXFLAGS = -std=c++17 -Wall

NET_H = ../socket/net.h

all: addrinfo echo_server echo_client http_client

addrinfo: addrinfo.c
	$(CC) $(CFLAGS) -o $@ addrinfo.c

echo_server: echo_server.c $(NET_H)
	$(CC) $(CFLAGS) -o $@ echo_server.c

echo_client: echo_client.c $(NET_H)
	$(CC) $(CFLAGS) -o $@ echo_client.c

# libcurl HTTP 客户端示例（配套 ../libcurl_cpp.md）
# 需要 libcurl 开发包：Debian/Ubuntu 上 `sudo apt install libcurl4-openssl-dev`
# 装好后 -lcurl 才能链接（-dev 提供 curl/curl.h 头和 libcurl.so 符号链接）。
http_client: http_client.cpp
	$(CXX) $(CXXFLAGS) -o $@ http_client.cpp -lcurl

clean:
	rm -f addrinfo echo_server echo_client http_client

.PHONY: all clean
