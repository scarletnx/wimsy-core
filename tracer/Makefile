# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
#
#   make [CONCORD=prefix]  tracer, linked with Concord v3.0.1 as make install PREFIX=prefix put it, libcurl, SQLite
#                          and libarchive; and tracer-mcp, its MCP server, with libmicrohttpd
#   make test              the tests: no Discord, no network, no Concord, no libcurl
#   make test-gateway      tracer (CONCORD as above) against a fake Discord whose gateway dies under it: Linux, as
#                          root or with user namespaces (../rei/tests/test_gateway.sh)
#
# BSD make and GNU make alike. ReiWimsy's common pieces (rei/src: the strings, the log, Discord's REST API) are built
# here again as Tracer's. The log summary is ScarletNX's (third_party/logscan, scripts/sync-logscan.sh), its demangler
# the C++ runtime's: CXXRT=-lc++ on FreeBSD.

CC ?= cc
CFLAGS ?= -O2 -g
CONCORD ?= /usr/local
CURL_CFLAGS ?= `pkg-config --cflags libcurl`
CURL_LIBS ?= `pkg-config --libs libcurl`
SQLITE_CFLAGS ?= `pkg-config --cflags sqlite3`
SQLITE_LIBS ?= `pkg-config --libs sqlite3`
ARCHIVE_LIBS ?= -larchive
ZLIB_LIBS ?= -lz
MHD_CFLAGS ?= `pkg-config --cflags libmicrohttpd`
MHD_LIBS ?= `pkg-config --libs libmicrohttpd`
CXXRT ?= -lstdc++
REI = ../rei
WARN = -std=c99 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Werror
# the log summary's version: its files' checksum, so a summary synced again makes the reports' summaries again
LOGSCAN_FILES = third_party/logscan/common/utilities/logscan.c third_party/logscan/common/utilities/logscan.h
DEFS = -D_POSIX_C_SOURCE=200809L -DREI_NAME=\"tracer\" -DTR_LOGSCAN=\"`cat $(LOGSCAN_FILES) | cksum | cut -d' ' -f1`\"
OURS = $(WARN) $(DEFS) -Isrc -I$(REI)/src -I$(REI)/third_party/cjson -Ithird_party/logscan
LIBS = $(SQLITE_LIBS) $(ARCHIVE_LIBS) $(ZLIB_LIBS) $(CXXRT) -lpthread -lm

LOGIC = src/message.o src/files.o src/probe.o src/rules.o src/store.o src/site.o src/report.o src/titles.o \
	src/channels.o src/worker.o src/issues.o src/age.o src/mcp.o src/rei_util.o third_party/cjson.o third_party/logscan.o \
	third_party/demangle.o

all: tracer tracer-mcp

tracer: $(LOGIC) src/rei_http.o src/rei_rest.o src/rei_watch.o src/fetch.o src/main.o
	$(CC) $(CFLAGS) -o $@ $(LOGIC) src/rei_http.o src/rei_rest.o src/rei_watch.o src/fetch.o src/main.o \
		$(CONCORD)/lib/libdiscord.a $(CURL_LIBS) $(LIBS)

tracer-mcp: src/mcp.o src/mcp_main.o src/rei_util.o third_party/cjson.o
	$(CC) $(CFLAGS) -o $@ src/mcp.o src/mcp_main.o src/rei_util.o third_party/cjson.o $(MHD_LIBS) -lm

test: tests/test_tracer
	./tests/test_tracer

test-gateway: tracer
	sh $(REI)/tests/test_gateway.sh tracer ./tracer
	sh $(REI)/tests/test_gateway.sh tracer-all ./tracer
	sh $(REI)/tests/test_gateway.sh tracer-zombie ./tracer
	sh $(REI)/tests/test_gateway.sh tracer-unready ./tracer

tests/test_tracer: $(LOGIC) tests/test_tracer.c
	$(CC) $(CFLAGS) $(OURS) $(SQLITE_CFLAGS) -o $@ tests/test_tracer.c $(LOGIC) $(LIBS)

src/message.o: src/message.c src/message.h src/tracer.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/message.c
src/files.o: src/files.c src/files.h src/probe.h src/tracer.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/files.c
src/probe.o: src/probe.c src/probe.h src/files.h src/tracer.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/probe.c
src/rules.o: src/rules.c src/rules.h src/store.h src/probe.h src/tracer.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/rules.c
src/store.o: src/store.c src/store.h src/probe.h src/tracer.h
	$(CC) $(CFLAGS) $(OURS) $(SQLITE_CFLAGS) -c -o $@ src/store.c
src/site.o: src/site.c src/site.h src/files.h src/rules.h src/store.h src/probe.h src/tracer.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/site.c
src/worker.o: src/worker.c src/worker.h src/report.h src/age.h src/store.h src/probe.h src/tracer.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/worker.c
src/issues.o: src/issues.c src/issues.h src/store.h src/probe.h src/tracer.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/issues.c
src/age.o: src/age.c src/age.h src/store.h src/probe.h src/tracer.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/age.c
src/channels.o: src/channels.c src/channels.h src/tracer.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/channels.c
src/titles.o: src/titles.c src/titles.h src/tracer.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/titles.c
src/report.o: src/report.c src/report.h src/age.h src/issues.h src/titles.h src/files.h src/site.h src/store.h \
		src/probe.h src/message.h src/tracer.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/report.c
src/mcp.o: src/mcp.c src/mcp.h src/tracer.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ src/mcp.c
src/mcp_main.o: src/mcp_main.c src/mcp.h src/tracer.h
	$(CC) $(CFLAGS) $(OURS) $(MHD_CFLAGS) -c -o $@ src/mcp_main.c
src/fetch.o: src/fetch.c src/fetch.h src/tracer.h
	$(CC) $(CFLAGS) $(OURS) $(CURL_CFLAGS) -c -o $@ src/fetch.c
# Concord's headers are not ours: their warnings are theirs
src/main.o: src/main.c $(LOGSCAN_FILES) src/age.h src/channels.h src/issues.h src/worker.h src/fetch.h src/report.h \
		src/site.h src/store.h src/probe.h src/message.h src/tracer.h $(REI)/src/watch.h
	$(CC) $(CFLAGS) $(OURS) $(CURL_CFLAGS) -isystem $(CONCORD)/include/concord -c -o $@ src/main.c
src/rei_util.o: $(REI)/src/util.c $(REI)/src/rei.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ $(REI)/src/util.c
src/rei_http.o: $(REI)/src/http.c $(REI)/src/http.h $(REI)/src/rei.h
	$(CC) $(CFLAGS) $(OURS) $(CURL_CFLAGS) -c -o $@ $(REI)/src/http.c
src/rei_rest.o: $(REI)/src/rest.c $(REI)/src/rest.h $(REI)/src/rei.h
	$(CC) $(CFLAGS) $(OURS) -c -o $@ $(REI)/src/rest.c
src/rei_watch.o: $(REI)/src/watch.c $(REI)/src/watch.h $(REI)/src/rei.h
	$(CC) $(CFLAGS) $(OURS) $(CURL_CFLAGS) -isystem $(CONCORD)/include/concord -c -o $@ $(REI)/src/watch.c
# cJSON and the log summary as their projects build them
third_party/cjson.o: $(REI)/third_party/cjson/cJSON.c $(REI)/third_party/cjson/cJSON.h
	$(CC) $(CFLAGS) -std=c89 -c -o $@ $(REI)/third_party/cjson/cJSON.c
third_party/logscan.o: third_party/logscan/common/utilities/logscan.c third_party/logscan/common/utilities/logscan.h
	$(CC) $(CFLAGS) -std=c99 -Ithird_party/logscan -c -o $@ third_party/logscan/common/utilities/logscan.c
third_party/demangle.o: third_party/logscan/common/utilities/demangle.c
	$(CC) $(CFLAGS) -std=c99 -Ithird_party/logscan -c -o $@ third_party/logscan/common/utilities/demangle.c

clean:
	rm -f $(LOGIC) src/rei_http.o src/rei_rest.o src/rei_watch.o src/fetch.o src/main.o src/mcp_main.o tracer tracer-mcp \
		tests/test_tracer

.PHONY: all test test-gateway clean
