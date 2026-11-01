#!/usr/bin/env bash
# Moves the fork (branch "atmosphere", checked out in atmosphere/) onto a new upstream release: scripts/update-atmosphere.sh 1.13.0
# Our hook commits are rebased on the tag; a conflict stops the rebase inside atmosphere/ for manual resolution.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
AMS="$ROOT/atmosphere"
TAG="${1:?usage: update-atmosphere.sh <upstream tag>}"
UPSTREAM="https://github.com/Atmosphere-NX/Atmosphere.git"

git -C "$AMS" remote get-url upstream >/dev/null 2>&1 || git -C "$AMS" remote add upstream "$UPSTREAM"
git -C "$AMS" remote set-url --push upstream DISABLED
# Upstream tags stay out of refs/tags (they would clash with our v* releases and are never pushed).
git -C "$AMS" fetch -q --no-tags upstream "refs/tags/$TAG:refs/upstream/tags/$TAG"

# The hook commits are the ones not in upstream; their parent is the release the fork is based on.
BASE="$(git -C "$AMS" merge-base atmosphere "refs/upstream/tags/$TAG")"
git -C "$AMS" checkout -q atmosphere
git -C "$AMS" rebase --onto "refs/upstream/tags/$TAG" "$BASE" atmosphere

echo "$TAG" > "$ROOT/ATMOSPHERE_VERSION"

echo "atmosphere rebased onto Atmosphère $TAG; build, test, then:"
echo "  git -C atmosphere push --force-with-lease origin atmosphere"
echo "  git add atmosphere ATMOSPHERE_VERSION && git commit -m 'chore(atmosphere): update to $TAG'"
