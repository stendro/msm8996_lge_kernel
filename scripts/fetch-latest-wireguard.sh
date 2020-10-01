#!/bin/bash
set -e
USER_AGENT="WireGuard-AndroidROMBuild/0.2 ($(uname -a))"

exec 9>.wireguard-fetch-lock
flock -n 9 || exit 0

[[ $(( $(date +%s) - $(stat -c %Y "net/wireguard/.check" 2>/dev/null || echo 0) )) -gt 86400 ]] || exit 0

while read -r distro package version _; do
	if [[ $distro == upstream && $package == kmodtools ]]; then
		VERSION="$version"
		break
	fi
done < <(curl -A "$USER_AGENT" -LSs --connect-timeout 30 https://build.wireguard.com/distros.txt)

[[ -n $VERSION ]]

if [[ -f net/wireguard/version.h && $(< net/wireguard/version.h) == *$VERSION* ]]; then
	touch net/wireguard/.check
	exit 0
fi

rm -rf net/wireguard
mkdir -p net/wireguard
VERSIONWG="0.0.20191219"
wget "https://download.wireguard.com/monolithic-historical/WireGuard-$VERSIONWG.tar.xz"
tar -C "net/wireguard/" -xf "WireGuard-$VERSIONWG.tar.xz" --strip-components=2 "WireGuard-$VERSIONWG/src"
rm "WireGuard-$VERSIONWG.tar.xz"
sed -i 's/tristate/bool/;s/default m/default y/;' net/wireguard/Kconfig
touch net/wireguard/.check


