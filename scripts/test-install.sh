#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
#
# install.sh against fake jails: jls, jexec and pkg are scripts of this test on PATH, a jail's root a folder, a package a
# text file of its name, its dependencies and its files, and a service runs in a jail when a file says so. What
# install.sh asks of each jail is logged, and each case checks what it restarted:
#
#   sh scripts/test-install.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT
mkdir "$t/bin"

# jls -j JAIL jid|path: a jail runs when its root is there
cat > "$t/bin/jls" <<'EOF'
#!/bin/sh
[ -d "$FAKE/root/$2" ] || exit 1
[ "$3" = path ] && echo "$FAKE/root/$2"
exit 0
EOF
# pkg query -F FILE FORMAT: %n the package's name, %dn its dependencies, %Fp its files, from FILE's "name", "dep" and
# "file" lines
cat > "$t/bin/pkg" <<'EOF'
#!/bin/sh
[ "$1 $2" = "query -F" ] || { echo "fake pkg: not a query of a file: $*" >&2; exit 64; }
case "$4" in
%n) sed -n 's/^name //p' "$3" ;;
%dn) sed -n 's/^dep //p' "$3" ;;
%Fp) sed -n 's/^file //p' "$3" ;;
*) echo "fake pkg: no format $4" >&2; exit 64 ;;
esac
EOF
# jexec JAIL COMMAND...: logged; service X status succeeds when running/JAIL/X is there; pkg query '%n %v' NAME a
# version
cat > "$t/bin/jexec" <<'EOF'
#!/bin/sh
jail=$1
shift
echo "$jail: $*" >> "$FAKE/log"
case "$1 $3" in
"service status") [ -e "$FAKE/running/$jail/$2" ] ;;
"pkg %n %v") echo "$4 1.0.0" ;;
esac
EOF
chmod +x "$t/bin/jls" "$t/bin/pkg" "$t/bin/jexec"

printf 'name reiwimsy\ndep curl\ndep ca_root_nss\nfile /usr/local/libexec/reiwimsy\nfile /usr/local/etc/rc.d/reiwimsy\n' \
	> "$t/reiwimsy.pkg"
printf 'name tracer\ndep curl\nfile /usr/local/libexec/tracer\nfile /usr/local/libexec/tracer-mcp\n%s\n%s\n' \
	'file /usr/local/etc/rc.d/tracer' 'file /usr/local/etc/rc.d/tracer_mcp' > "$t/tracer.pkg"
printf 'name accordwimsy\nfile /usr/local/bin/accordwimsy\nfile /usr/local/etc/accordwimsy/accordwimsy.env.sample\n' \
	> "$t/accordwimsy.pkg"
fails=0

# Installs <package> into <jail>, the services <running> running there; expects install.sh's exit <want> and the log
# to hold each line of <has> and none of <hasnot> (a | between lines)
case_() {
	rm -rf "$t/root" "$t/running" "$t/log"
	mkdir -p "$t/root/$2/tmp" "$t/running/$2"
	: > "$t/log"
	for s in $4; do : > "$t/running/$2/$s"; done
	# the package's rc scripts in the jail, as its pkg add leaves them
	mkdir -p "$t/root/$2/usr/local/etc/rc.d"
	sed -n 's|^file /usr/local/etc/rc\.d/||p' "$t/$3.pkg" | while read -r rc; do
		install -m 555 /dev/null "$t/root/$2/usr/local/etc/rc.d/$rc"
	done
	got=0
	FAKE=$t PATH="$t/bin:$PATH" sh "$here/install.sh" "$2" "$t/$3.pkg" > "$t/out" 2>&1 || got=$?
	why=
	[ "$got" -eq "$5" ] || why="exit $got, not $5"
	old_ifs=$IFS
	IFS='|'
	for line in $6; do grep -qxF "$line" "$t/log" || why="${why:+$why; }no \"$line\""; done
	for line in ${7:-}; do ! grep -qxF "$line" "$t/log" || why="${why:+$why; }\"$line\""; done
	IFS=$old_ifs
	if [ -z "$why" ]; then
		echo "ok   $1"
	else
		echo "FAIL $1: $why; its log:"
		sed 's/^/     /' "$t/log" "$t/out"
		fails=$((fails + 1))
	fi
}

case_ "tracer_mcp, which runs where the site is served, restarted there" tracerweb tracer tracer_mcp 0 \
	"tracerweb: pkg install -y curl|tracerweb: service tracer_mcp restart" "tracerweb: service tracer restart"
case_ "tracer restarted in its own jail, tracer_mcp left off" tracer tracer tracer 0 \
	"tracer: service tracer restart" "tracer: service tracer_mcp restart"
case_ "both restarted where both run" tracer tracer "tracer tracer_mcp" 0 \
	"tracer: service tracer restart|tracer: service tracer_mcp restart"
case_ "a service not running stays so" discordbots reiwimsy "" 0 \
	"discordbots: pkg install -y curl ca_root_nss|discordbots: pkg add -f /tmp/reiwimsy.pkg" \
	"discordbots: service reiwimsy restart"
case_ "a package with no service and no dependency" gitsync accordwimsy "" 0 "gitsync: pkg add -f /tmp/accordwimsy.pkg" \
	"gitsync: pkg install -y"
rm -rf "$t/root"
mkdir "$t/root"
if FAKE=$t PATH="$t/bin:$PATH" sh "$here/install.sh" nojail "$t/reiwimsy.pkg" > "$t/out" 2>&1 ||
	! grep -q '^nojail is not running' "$t/out"; then
	echo "FAIL a jail that is not running stops it, saying so"
	fails=$((fails + 1))
else
	echo "ok   a jail that is not running stops it, saying so"
fi
[ "$fails" -eq 0 ]
