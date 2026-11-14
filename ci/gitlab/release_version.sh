#!/bin/sh
# GitLab stand-in for release.yml's "version" job: decides the version and whether to publish, and writes
# them as a dotenv report (version.env) that the later release jobs receive as variables:
#   GH_NEEDS_VERSION_VERSION / _PUBLISH / _DEVEL / _SKIP   (needs.version.outputs.* in release.yml)
#   GH_INPUT_DEVEL_BUILD / GH_INPUT_DRY_RUN                (inputs.* in release.yml)
#
#   tag pipeline on vX.Y.Z                 version vX.Y.Z, published (WWHD_DRY_RUN=1: built only)
#   WWHD_VERSION=vX.Y.Z on a branch        the same, but the tag must already exist here and point at this commit
#   WWHD_DEVEL_BUILD=1                     <newest vX.Y.Z tag>-devel.<commit>, built only (job artifacts)
#   otherwise                              dryrun-<commit>, built only
set -eu
sha=$CI_COMMIT_SHA
short=$(printf '%s' "$sha" | cut -c1-8)
publish=false
devel=false
tags=$(git ls-remote --tags "${CI_REPOSITORY_URL:-origin}" 'v*')

if [ "${WWHD_DEVEL_BUILD:-}" = 1 ]; then
  base=$(printf '%s\n' "$tags" | sed -n 's#.*refs/tags/\(v[0-9]*\.[0-9]*\.[0-9]*\)$#\1#p' | sort -V | tail -1)
  [ -n "$base" ] || { echo "no vX.Y.Z tag in this project"; exit 1; }
  version="$base-devel.$short"
  devel=true
elif [ -n "${CI_COMMIT_TAG:-}" ] || [ -n "${WWHD_VERSION:-}" ]; then
  version=${CI_COMMIT_TAG:-$WWHD_VERSION}
  case "$version" in v[0-9]*) ;; *) echo "release versions are tags v*: got '$version'"; exit 1 ;; esac
  # the tag's commit (annotated tags: the peeled ^{} line)
  tag_sha=$(printf '%s\n' "$tags" | awk -v t="refs/tags/$version" '$2 == t || $2 == t "^{}" {s = $1} END {print s}')
  [ -n "$tag_sha" ] || { echo "tag $version does not exist in this project (the mirror brings tags from GitHub)"; exit 1; }
  [ "$tag_sha" = "$sha" ] || { echo "tag $version is $tag_sha, this pipeline runs on $sha: run it on the tag"; exit 1; }
  [ "${WWHD_DRY_RUN:-}" = 1 ] || publish=true
else
  version="dryrun-$short"
fi

dry=true
[ "$publish" = true ] && dry=false
cat > version.env <<EOF
GH_NEEDS_VERSION_VERSION=$version
GH_NEEDS_VERSION_PUBLISH=$publish
GH_NEEDS_VERSION_DEVEL=$devel
GH_NEEDS_VERSION_SKIP=false
GH_INPUT_DEVEL_BUILD=$devel
GH_INPUT_DRY_RUN=$dry
EOF
cat version.env
