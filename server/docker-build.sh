#!/bin/sh
# Builds the wwhd-online-server image from a small context: only server/ and runtime/src/online/proto.
# Usage: server/docker-build.sh [tag]   (run from anywhere inside the repository)
set -eu
tag=${1:-wwhd-online-server}
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
ctx=$(mktemp -d)
trap 'rm -rf "$ctx"' EXIT   # the context this script made, removed when it ends
mkdir -p "$ctx/runtime/src/online"
cp -R "$root/server" "$ctx/server"
cp -R "$root/runtime/src/online/proto" "$ctx/runtime/src/online/proto"
docker build -f "$ctx/server/Dockerfile" -t "$tag" "$ctx"
