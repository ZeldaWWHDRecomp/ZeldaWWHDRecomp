#!/bin/bash
# GitLab Linux job (Docker executor, ubuntu:22.04 / ubuntu:24.04 image, run as root): gives the container what a
# GitHub Ubuntu runner has before the first step (git, curl, compilers, Python, sudo; with --android a JDK 17
# and the Android SDK command-line tools at $ANDROID_HOME), creates the unprivileged user "runner" with
# passwordless sudo, as on GitHub (tests that check file permissions behave differently as root), and runs the
# GitHub job's steps as that user with ci/gitlab/gh_steps.py.
#
#   ci/gitlab/linux_job.sh [--android] WORKFLOW JOB [gh_steps.py options]
set -euo pipefail
android=0
if [ "${1:-}" = --android ]; then android=1; shift; fi
[ "$(id -u)" = 0 ] || { echo "linux_job.sh expects the image's root user"; exit 1; }

export DEBIAN_FRONTEND=noninteractive
# apt gives up and retries instead of hanging (.github/actions/apt-timeouts)
echo 'Acquire::Retries "3"; Acquire::http::Timeout "30"; Acquire::https::Timeout "30";' > /etc/apt/apt.conf.d/80-ci-timeouts
apt-get update -qq
pkgs=(sudo git ca-certificates curl wget python3 python3-yaml python3-venv python3-pip build-essential clang lld
      cmake ninja-build pkg-config file unzip zip xz-utils zstd jq tzdata locales)
[ "$android" = 1 ] && pkgs+=(openjdk-17-jdk-headless)
apt-get install -y -qq --no-install-recommends "${pkgs[@]}" > /dev/null
locale-gen C.UTF-8 > /dev/null 2>&1 || true

id runner > /dev/null 2>&1 || useradd -m -s /bin/bash runner
echo 'runner ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/runner
chmod 440 /etc/sudoers.d/runner

if [ "$android" = 1 ]; then
  # Android SDK command-line tools 19.0 (pinned; sdkmanager then checks every package it installs)
  : "${ANDROID_HOME:=/opt/android-sdk}"
  export ANDROID_HOME ANDROID_SDK_ROOT="$ANDROID_HOME"
  url=https://dl.google.com/android/repository/commandlinetools-linux-13114758_latest.zip
  sum=7ec965280a073311c339e571cd5de778b9975026cfcbe79f2b1cdcb1e15317ee
  tmp=$(mktemp -d)
  curl -fsSL --retry 3 -o "$tmp/clt.zip" "$url"
  echo "$sum  $tmp/clt.zip" | sha256sum -c -
  mkdir -p "$ANDROID_HOME/cmdline-tools"
  unzip -q "$tmp/clt.zip" -d "$tmp"
  rm -rf "$ANDROID_HOME/cmdline-tools/latest"
  mv "$tmp/cmdline-tools" "$ANDROID_HOME/cmdline-tools/latest"
  rm -rf "$tmp"
  yes | "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --licenses > /dev/null || true
  chown -R runner "$ANDROID_HOME"
  export JAVA_HOME
  JAVA_HOME=$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")
fi

# the checkout, GitLab's restored cache and earlier jobs' artifacts belong to root
chown -R runner "$CI_PROJECT_DIR"
git config --system --add safe.directory '*'
cd "$CI_PROJECT_DIR"
exec runuser -u runner -- env HOME=/home/runner python3 ci/gitlab/gh_steps.py "$@"
