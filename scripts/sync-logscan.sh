#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
#
# Copies ScarletNX's log summary (src/common/utilities: logscan.c and .h, demangle.c and .h, ascii.h) into
# tracer/third_party/logscan, as that checkout has them, and writes its commit to COMMIT there:
#
#   sh scripts/sync-logscan.sh SCARLETNX
#
# Tracer's tests run after: a summary that changed what it reports shows there.
set -eu

if [ $# -ne 1 ] || [ ! -f "$1/src/common/utilities/logscan.c" ]; then
	echo "usage: sh scripts/sync-logscan.sh SCARLETNX (a checkout of ScarletNX)" >&2
	exit 1
fi
top=$(cd "$(dirname "$0")/.." && pwd)
to=$top/tracer/third_party/logscan
for f in logscan.c logscan.h demangle.c demangle.h ascii.h; do
	cp "$1/src/common/utilities/$f" "$to/common/utilities/$f"
done
git -C "$1" rev-parse HEAD > "$to/COMMIT"
echo "tracer/third_party/logscan at $(cat "$to/COMMIT")"
make -s -C "$top/tracer" clean
make -s -C "$top/tracer" test
