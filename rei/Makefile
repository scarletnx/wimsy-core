# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
#
#   make [CONCORD=prefix]  reiwimsy, linked with Concord v3.0.1 as make install PREFIX=prefix put it, libcurl, and
#                          Tracer (../tracer/libtracer.a, which it runs beside ReiWimsy with TRACER_TOKEN) with its
#                          SQLite, libarchive, zlib and C++ runtime (CXXRT=-lc++ on FreeBSD)
#   make test              the tests: no Discord, no network, no Concord
#   make test-gateway      reiwimsy (CONCORD as above) against a fake Discord whose gateway dies under it: Linux, as
#                          root or with user namespaces (tests/test_gateway.sh)
#
# BSD make and GNU make alike.

CC ?= cc
CFLAGS ?= -O2 -g
CONCORD ?= /usr/local
CURL_CFLAGS ?= `pkg-config --cflags libcurl`
CURL_LIBS ?= `pkg-config --libs libcurl`
SQLITE_LIBS ?= `pkg-config --libs sqlite3`
ARCHIVE_LIBS ?= -larchive
ZLIB_LIBS ?= -lz
CXXRT ?= -lstdc++
WARN = -std=c99 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Werror
DEFS = -D_POSIX_C_SOURCE=200809L
OURS = $(WARN) $(DEFS) -Isrc -Ithird_party/cjson

LOGIC = src/util.o src/refs.o src/github.o src/look.o src/link.o src/cards.o src/state.o src/rest.o \
	src/event.o third_party/cjson/cJSON.o

all: reiwimsy

reiwimsy: $(LOGIC) src/http.o src/watch.o src/bot.o src/main.o tracer
	$(CC) $(CFLAGS) -o $@ src/main.o src/bot.o ../tracer/libtracer.a $(LOGIC) src/http.o src/watch.o \
		$(CONCORD)/lib/libdiscord.a $(CURL_LIBS) $(SQLITE_LIBS) $(ARCHIVE_LIBS) $(ZLIB_LIBS) $(CXXRT) -lpthread -lm

# Tracer's bot, as its Makefile builds it
tracer:
	cd ../tracer && $(MAKE) libtracer.a CONCORD="$(CONCORD)" CC="$(CC)" CFLAGS="$(CFLAGS)"

test: tests/test_rei
	./tests/test_rei

test-gateway: reiwimsy
	sh tests/test_gateway.sh reiwimsy ./reiwimsy
	sh tests/test_gateway.sh zombie ./reiwimsy
	sh tests/test_gateway.sh unready ./reiwimsy
	sh tests/test_gateway.sh stalled ./reiwimsy
	sh tests/test_gateway.sh nightly ./reiwimsy
	sh tests/test_gateway.sh both ./reiwimsy

tests/test_rei: $(LOGIC) tests/test_rei.c
	$(CC) $(CFLAGS) $(OURS) -o $@ tests/test_rei.c $(LOGIC) -lpthread -lm

src/util.o: src/util.c src/rei.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/util.c
src/refs.o: src/refs.c src/refs.h src/rei.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/refs.c
src/github.o: src/github.c src/github.h src/rei.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/github.c
src/look.o: src/look.c src/look.h src/rei.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/look.c
src/link.o: src/link.c src/link.h src/look.h src/github.h src/refs.h src/rei.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/link.c
src/cards.o: src/cards.c src/cards.h src/look.h src/github.h src/rei.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/cards.c
src/state.o: src/state.c src/state.h src/rei.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/state.c
src/rest.o: src/rest.c src/rest.h src/github.h src/rei.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/rest.c
src/event.o: src/event.c src/event.h src/rei.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/event.c
src/http.o: src/http.c src/http.h src/github.h src/rei.h
	$(CC) $(CFLAGS) $(OURS) $(CURL_CFLAGS) -c -o $@ src/http.c
# Concord's headers are not ours: their warnings are theirs
src/bot.o: src/bot.c src/bot.h src/cards.h src/event.h src/http.h src/link.h src/rest.h src/state.h src/watch.h \
		src/rei.h
	$(CC) $(CFLAGS) $(OURS) $(CURL_CFLAGS) -isystem $(CONCORD)/include/concord -c -o $@ src/bot.c
src/main.o: src/main.c src/bot.h ../tracer/src/bot.h src/rei.h
	$(CC) $(CFLAGS) $(OURS) -I.. $(CURL_CFLAGS) -isystem $(CONCORD)/include/concord -c -o $@ src/main.c
src/watch.o: src/watch.c src/watch.h src/rei.h
	$(CC) $(CFLAGS) $(OURS) $(CURL_CFLAGS) -isystem $(CONCORD)/include/concord -c -o $@ src/watch.c
# cJSON as its project builds it
third_party/cjson/cJSON.o: third_party/cjson/cJSON.c third_party/cjson/cJSON.h
	$(CC) $(CFLAGS) -std=c89 -c -o $@ third_party/cjson/cJSON.c

clean:
	rm -f $(LOGIC) src/http.o src/watch.o src/bot.o src/main.o reiwimsy tests/test_rei

.PHONY: all test test-gateway clean tracer
