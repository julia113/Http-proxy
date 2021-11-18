CC = gcc

CCFLAGS = -Wall -Wextra -Wpedantic -Wshadow

httpproxy: httpproxy.o cache.o
	$(CC) -o httpproxy httpproxy.o cache.o
