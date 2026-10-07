#!/bin/sh
# Builds the module for win32, win64, linux x86 and linux x86_64 in Docker
# and writes the binaries to pg/bin, with the licenses that go with them.
# Extra arguments are passed to `docker build`, e.g. ./build.sh --no-cache
set -eu

cd "$(dirname "$0")"

"${DOCKER:-docker}" build --platform linux/amd64 \
  --output type=local,dest=pg/bin "$@" .

ls -l pg/bin
