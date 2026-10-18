#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
make clean
./tools/make-orig-tarball.sh
dpkg-buildpackage -S -us -uc
printf '
Debian source artifacts written to the parent directory.
'
