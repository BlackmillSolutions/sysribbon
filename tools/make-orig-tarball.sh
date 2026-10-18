#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
VERSION="$(dpkg-parsechangelog -SVersion | sed 's/-[^-]*$//')"
DEST="$(cd .. && pwd)/sysribbon_${VERSION}.orig.tar.gz"
# Upstream orig archive MUST NOT contain debian/ packaging directory.
tar -czf "$DEST" --transform="s,^,sysribbon-${VERSION}/," \
  Makefile LICENSE README.md MIGRATION.md PACKAGING.md .gitignore sysribbon.desktop src tests assets tools
printf 'Created %s
' "$DEST"
