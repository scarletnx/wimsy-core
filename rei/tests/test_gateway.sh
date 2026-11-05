#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
#
# A bot built with Concord, its gateway dying under it: ReiWimsy with its rounds held inside a GitHub request, Tracer
# just up. Each has to end with status 1, which daemon(8) starts it again from, saying that Concord's GET gateway/bot
# failed and what Discord answers it now. fix-cards: reiwimsy --fix-cards on a nightly channel with two cards saying
# "Building" (fake_discord.py's cards): the dry run changes nothing, --apply deletes the one of a commit inside a push
# and redraws the push's head with its failed build, and a second --apply finds nothing to do.
#
#   sh rei/tests/test_gateway.sh reiwimsy|tracer|fix-cards BINARY
#
# Linux, no network: the bot runs in a mount and network namespace of its own, and in a user namespace too when not run
# by root (a machine that keeps those from users runs the test as root), whose /etc/hosts sends Discord and GitHub to
# fake_discord.py on 127.0.0.1 and whose CA bundle (libcurl's, curl-config --ca) holds only the CA this script makes.
# Needs unshare and ip (util-linux, iproute2), python3, openssl and curl-config.
set -eu
program=${1:-} binary=${2:-}
case "$program" in
reiwimsy) drop_after=github ;;
tracer) drop_after=ready ;;
fix-cards) drop_after=cards ;;
*)
	echo "usage: sh test_gateway.sh reiwimsy|tracer|fix-cards BINARY (program \"$program\")" >&2
	exit 2
	;;
esac
[ -x "$binary" ] || { echo "test_gateway.sh: $binary is not a program to run" >&2; exit 2; }
for tool in unshare ip python3 openssl curl-config; do
	command -v "$tool" > /dev/null || { echo "test_gateway.sh: $tool is not installed" >&2; exit 2; }
done
ca=$(curl-config --ca)
[ -n "$ca" ] && [ -f "$ca" ] ||
	{ echo "test_gateway.sh: curl-config --ca names \"$ca\", not libcurl's CA bundle file" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
binary=$(cd "$(dirname "$binary")" && pwd)/$(basename "$binary")
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj /CN=fake-ca -keyout "$tmp/ca.key" -out "$tmp/ca.pem" \
	2> /dev/null
openssl req -newkey rsa:2048 -nodes -subj /CN=fake -keyout "$tmp/leaf.key" -out "$tmp/leaf.csr" 2> /dev/null
printf 'subjectAltName=DNS:discord.com,DNS:gateway.discord.gg,DNS:api.github.com\n' > "$tmp/san.ext"
openssl x509 -req -in "$tmp/leaf.csr" -CA "$tmp/ca.pem" -CAkey "$tmp/ca.key" -CAcreateserial -days 2 \
	-extfile "$tmp/san.ext" -out "$tmp/leaf.pem" 2> /dev/null
printf '127.0.0.1 localhost discord.com gateway.discord.gg api.github.com\n' > "$tmp/hosts"
# a token of Discord's form, which Concord checks before anything
token=FakeFakeFakeFakeFakeFake.Fake00.FakeFakeFakeFakeFakeFakeFake
mkdir "$tmp/site"
if [ "$program" = reiwimsy ]; then
	set -- REIWIMSY_TOKEN=$token REIWIMSY_REPO=owner/repo GITHUB_TOKEN=fake REIWIMSY_CARDS_NIGHTLY=1 \
		REIWIMSY_STATE="$tmp/state.json"
elif [ "$program" = fix-cards ]; then
	set -- REIWIMSY_TOKEN=$token REIWIMSY_REPO=owner/repo GITHUB_TOKEN=fake REIWIMSY_CARDS_NIGHTLY=5
else
	set -- TRACER_TOKEN=$token TRACER_URL=http://127.0.0.1:8 TRACER_DB="$tmp/tracer.db" TRACER_SITE="$tmp/site" \
		TRACER_ASSETS="$top/tracer/site"
fi

# root keeps the real one: in a user namespace of its own, its root would lose its rights over the files of the
# checkout's owner (the namespace maps no other user)
if [ "$(id -u)" = 0 ]; then namespaces=-nm; else namespaces=-rnm; fi
unshare $namespaces sh -c '
	tmp=$1 ca=$2 here=$3 drop_after=$4 binary=$5
	shift 5
	ip link set lo up
	mount --bind "$tmp/hosts" /etc/hosts
	mount --bind "$tmp/ca.pem" "$ca"
	python3 -I "$here/fake_discord.py" "$tmp/leaf.pem" "$tmp/leaf.key" "$drop_after" 2>> "$tmp/fake.log" &
	fake=$!
	tries=0
	until grep -qs listening "$tmp/fake.log"; do
		tries=$((tries + 1))
		if [ $tries -gt 100 ] || ! kill -0 $fake 2> /dev/null; then
			echo "test_gateway.sh: fake_discord.py did not start:" >&2
			cat "$tmp/fake.log" >&2
			exit 2
		fi
		sleep 0.1
	done
	set +e
	if [ "$drop_after" = cards ]; then
		n=0
		for apply in "" --apply --apply; do
			n=$((n + 1))
			echo "--- run $n" >> "$tmp/fake.log"
			env -i PATH=/usr/bin:/bin "$@" timeout 60 "$binary" --fix-cards $apply 2> "$tmp/bot$n.log"
			echo $? > "$tmp/status$n"
		done
	else
		env -i PATH=/usr/bin:/bin "$@" timeout 60 "$binary" 2> "$tmp/bot.log"
		echo $? > "$tmp/status"
	fi
	kill $fake
' sh "$tmp" "$ca" "$here" "$drop_after" "$binary" "$@"

if [ "$program" = fix-cards ]; then
	# what each run changed on Discord, from the fake's steps between its markers
	changes() { sed -n "/^--- run $1\$/,/^--- run/p" "$tmp/fake.log" | grep -c '^discord: \(DELETE\|PATCH\)' || true; }
	totals() { grep 'fix-cards: [0-9]* messages read' "$tmp/bot$1.log"; }
	failed=
	for n in 1 2 3; do
		[ "$(cat "$tmp/status$n")" = 0 ] || failed="run $n ended with $(cat "$tmp/status$n"), not 0"
	done
	if [ -n "$failed" ]; then
		:
	elif grep -q 'no answer here' "$tmp/fake.log"; then
		failed="it asked GitHub for what the fake has no answer to"
	elif [ "$(changes 1)" != 0 ] ||
		! totals 1 | grep -q '2 of ours saying Building: 1 would be deleted, 1 would be redrawn, 0 failed'; then
		failed="the dry run did not say one to delete and one to redraw, changing nothing"
	elif ! grep -q 'bbbbbbb, .*, would be deleted: no push.s build ran for it' "$tmp/bot1.log" ||
		! grep -q 'ccccccc, .*, would be redrawn: Not published: the build of' "$tmp/bot1.log"; then
		failed="the dry run did not name the card inside a push and the head whose build failed"
	elif [ "$(changes 2)" != 2 ] || ! totals 2 | grep -q '1 deleted, 1 redrawn, 0 failed'; then
		failed="--apply did not delete one card and redraw the other"
	elif [ "$(changes 3)" != 0 ] || ! totals 3 | grep -q '0 of ours saying Building: 0 deleted, 0 redrawn, 0 failed'; then
		failed="a second --apply found something to do"
	fi
	if [ -n "$failed" ]; then
		echo "test_gateway.sh: fix-cards: $failed. The fake's steps, then each run's log:" >&2
		cat "$tmp/fake.log" "$tmp/bot1.log" "$tmp/bot2.log" "$tmp/bot3.log" >&2
		exit 1
	fi
	echo "fix-cards: the dry run changed nothing, --apply deleted 1 and redrew 1, again 0: $(totals 2)"
	exit 0
fi
status=$(cat "$tmp/status")
# the line Concord's failed GET gateway/bot ends with, Discord asked again: the fake closes the connection unanswered
said="^$program: the gateway stopped: Concord's GET gateway/bot failed, .*; asked again, Discord answers 0: GET "
said="${said}https://discord.com/api/v10/gateway/bot: "
failed=
if [ "$status" != 1 ]; then
	case "$status" in
	124) failed="it was still running after 60 s" ;;
	13[0-9] | 14[0-9]) failed="it ended on signal $((status - 128)), not with 1" ;;
	*) failed="it ended with $status, not 1" ;;
	esac
elif ! grep -q 'gateway: closed with 1001' "$tmp/fake.log"; then
	failed="the fake gateway never closed under it"
elif ! grep -q "$said" "$tmp/bot.log"; then
	failed="it never said its GET gateway/bot failed and what Discord answers it now"
fi
if [ -n "$failed" ]; then
	echo "test_gateway.sh: $program, its gateway closed: $failed. The fake's steps, then the end of its log:" >&2
	cat "$tmp/fake.log" >&2
	tail -n 15 "$tmp/bot.log" >&2
	exit 1
fi
echo "$program: ended with 1 when its gateway stopped: $(grep "$said" "$tmp/bot.log")"
