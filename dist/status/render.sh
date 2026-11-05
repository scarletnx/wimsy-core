#!/bin/sh
# Writes gatus.yaml from gatus.yaml.in, push.sh from push.sh.in, nginx.conf from nginx.conf.in and each
# sws/<theme>.css (the page's custom.css in a theme) from its .css.in: every @NAME@ becomes NAME's value in
# addresses.conf, and every @ICON_<BOT>@ the base64 of icons/<bot>.png (the name in lower case). The
# results are what the host runs; they are kept beside the templates so a review sees what runs,
# and check.sh fails when they are not up to date. @PUSH_TOKEN@ stays: install.sh fills it in there.
set -eu
cd "$(dirname "$0")"
sed_args=
while IFS='=' read -r name value; do
	case "$name" in '' | '#'*) continue ;; esac
	sed_args="$sed_args -e s|@$name@|$value|g"
	# NAME_JSON: the value as a JSON string writes it, each slash escaped (the logo Gatus puts in its page)
	json=$(printf '%s' "$value" | sed 's|/|\\\\/|g')
	sed_args="$sed_args -e s|@${name}_JSON@|$json|g"
done < addresses.conf
for icon in icons/*.png; do
	bot=$(basename "$icon" .png | tr '[:lower:]' '[:upper:]')
	sed_args="$sed_args -e s|@ICON_$bot@|$(openssl base64 -A -in "$icon")|g"
done
# shellcheck disable=SC2086
sed $sed_args gatus.yaml.in > gatus.yaml
# shellcheck disable=SC2086
sed $sed_args push.sh.in > push.sh
chmod 755 push.sh
# shellcheck disable=SC2086
sed $sed_args nginx.conf.in > nginx.conf
for css in sws/*.css.in; do
	# shellcheck disable=SC2086
	sed $sed_args "$css" > "${css%.in}"
done
