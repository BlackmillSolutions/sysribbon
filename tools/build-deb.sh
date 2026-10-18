#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
command -v dpkg-buildpackage >/dev/null || { echo 'Install dpkg-dev, debhelper, fakeroot and build dependencies.' >&2; exit 1; }
dpkg-buildpackage -b -us -uc
printf '
Binary .deb built in parent directory (../xfce4-sysribbon-plugin_*.deb).
'
