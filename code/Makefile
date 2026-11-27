# Linux / FreeBSD (no FreeBSD use: gmake, ou make com CC=cc)
CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
LDFLAGS ?= -pthread

all: bench test_platform

bench: bench.c platform_thread.c platform_thread.h
	$(CC) $(CFLAGS) -o $@ bench.c platform_thread.c $(LDFLAGS)

test_platform: ../testes/test_platform.c platform_thread.c platform_thread.h
	$(CC) $(CFLAGS) -I. -o $@ ../testes/test_platform.c platform_thread.c $(LDFLAGS)

test: test_platform
	./test_platform

clean:
	rm -f bench test_platform

.PHONY: all test clean
