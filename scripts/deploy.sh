#!/usr/bin/env bash
# Tests, builds, and ships the production image to the Raspberry Pi.
#
#   1. Runs the Dockerfile's test stage here (unit tests + coverage gate).
#   2. Builds the arm64 runtime image here with buildx (QEMU emulation), so
#      the Pi never compiles anything.
#   3. Streams the image to the Pi over ssh, copies docker-compose.yml, and
#      restarts the service without building.
#
# Run from Git Bash or any POSIX shell with Docker and the `pi` ssh alias:
#   scripts/deploy.sh
set -euo pipefail

cd "$(dirname "$0")/.."

PI_HOST="${PI_HOST:-pi}"
PI_DIR="${PI_DIR:-/home/pop/dev/Rocket_Launch_Notifier}"
SSH=(ssh -o BatchMode=yes "$PI_HOST")

echo "==> Running tests"
docker build --target test --progress=plain .

echo "==> Building arm64 runtime image"
docker buildx build --platform linux/arm64 --target runtime --load -t rocket-watcher:arm64 .

echo "==> Shipping image to $PI_HOST"
docker save rocket-watcher:arm64 | gzip | "${SSH[@]}" \
  'gunzip | docker load && docker tag rocket-watcher:arm64 rocket-watcher:latest'
scp -o BatchMode=yes docker-compose.yml "$PI_HOST:$PI_DIR/docker-compose.yml"

echo "==> Restarting on $PI_HOST"
"${SSH[@]}" "cd '$PI_DIR' && docker compose up -d --no-build && docker compose ps"
