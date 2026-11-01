#!/usr/bin/env bash
# Builds the atms-build-libnx Docker image used by scripts/build.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
docker build -t atms-build "$ROOT/atmosphere"
docker build -t atms-build-libnx --build-arg BASE_IMAGE=atms-build "$ROOT/docker"
