#!/usr/bin/env bash
# Makes a release: scripts/release.sh <version> [--publish | --publish-ci] [--skip-build]
#   <version>      0.1.0, v0.1.0 or v0.2.0-beta.1; 0.x and versions with a "-" are pre-releases
#   (default)      simulation: builds, packages and writes dist/release-<tag>/ (zip, notes.md, CHANGELOG.md) and prints
#                  what --publish would do; no tag or upload
#   --publish      builds on the PC, tags, and publishes the GitHub release from here; the Release workflow skips the
#                  build and only updates CHANGELOG.md
#   --publish-ci   tags and pushes the tag; the Release workflow builds, publishes and updates CHANGELOG.md
# CHANGELOG.md on master is always committed by the Release workflow, never by this script.
#   --skip-build   packages the last build instead of building again
# Needs Docker, git-cliff (cliff.toml) and, to publish, gh (also lends git-cliff its token for authors and pull requests).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PUBLISH=false
PUBLISH_CI=false
SKIP_BUILD=false
VERSION=""
for arg in "$@"; do
    case "$arg" in
        --publish)    PUBLISH=true ;;
        --publish-ci) PUBLISH=true; PUBLISH_CI=true ;;
        --skip-build) SKIP_BUILD=true ;;
        -*)           echo "unknown option: $arg" >&2; exit 2 ;;
        *)            VERSION="$arg" ;;
    esac
done
[[ -n "$VERSION" ]] || { echo "usage: scripts/release.sh <version> [--publish | --publish-ci] [--skip-build]" >&2; exit 2; }

TAG="v${VERSION#v}"
[[ "$TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]] || { echo "not a version: $VERSION" >&2; exit 2; }
PRERELEASE=false
[[ "$TAG" == v0.* || "$TAG" == *-* ]] && PRERELEASE=true

problems=()
[[ -z "$(git status --porcelain)" ]] || problems+=("the working tree has changes")
[[ "$(git rev-parse --abbrev-ref HEAD)" == "master" ]] || problems+=("not on master")
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    problems+=("tag $TAG already exists")
fi
if git remote get-url origin >/dev/null 2>&1; then
    if timeout 30 git fetch -q origin master --tags; then
        [[ "$(git rev-parse HEAD)" == "$(git rev-parse origin/master)" ]] || problems+=("master is not the same as origin/master")
    else
        problems+=("could not reach origin to compare master")
    fi
fi
for problem in "${problems[@]}"; do
    if $PUBLISH; then
        echo "cannot publish: $problem" >&2
    else
        echo "warning: $problem (fine for a simulation, not for --publish)" >&2
    fi
done
if $PUBLISH && (( ${#problems[@]} > 0 )); then
    exit 1
fi

OUT="$ROOT/dist/release-$TAG"
rm -rf "${OUT:?}"
mkdir -p "$OUT"
if $PUBLISH_CI; then
    # The workflow builds and packages; only the changelog is made here.
    ZIP="$OUT/usb-loader-nx-$TAG.zip"
else
    if ! $SKIP_BUILD; then
        USBL_VERSION="$TAG" scripts/build.sh
    fi
    ZIP="$(USBL_VERSION="$TAG" scripts/package.sh)"
    mv "$ZIP" "$OUT/"
    ZIP="$OUT/$(basename "$ZIP")"
fi

# With a GitHub token, git-cliff shows each commit's author as their GitHub user and its pull request.
if [[ -z "${GITHUB_TOKEN:-}" ]] && command -v gh >/dev/null && gh auth status >/dev/null 2>&1; then
    export GITHUB_TOKEN="$(gh auth token)"
fi
scripts/release-notes.sh "$TAG" "$(basename "$ZIP")" "$OUT"

TITLE="USB Loader NX $TAG"
GH_FLAGS=(--title "$TITLE" --notes-file "$OUT/notes.md")
$PRERELEASE && GH_FLAGS+=(--prerelease)

if ! $PUBLISH; then
    echo
    echo "Simulated release $TAG ($($PRERELEASE && echo pre-release || echo release)) in ${OUT#"$ROOT/"}:"
    echo "  $(basename "$ZIP") ($(du -h "$ZIP" | cut -f1))"
    echo "  notes.md       the release notes"
    echo "  CHANGELOG.md   what the Release workflow will commit to master"
    echo
    echo "--publish would then run:"
    echo "  git tag -a $TAG -m '$TITLE'"
    echo "  gh release create $TAG $(basename "$ZIP") --draft ${GH_FLAGS[*]}"
    echo "  git push origin $TAG"
    echo "  gh release edit $TAG --draft=false"
    exit 0
fi

git tag -a "$TAG" -m "$TITLE"
if $PUBLISH_CI; then
    git push -q origin "$TAG"
    echo "Pushed $TAG: the Release workflow builds, publishes and updates CHANGELOG.md."
    exit 0
fi
# A draft release first, then the tag: the Release workflow finds the release already there, skips the build and only
# updates CHANGELOG.md. The draft goes public once its tag is on GitHub.
gh release create "$TAG" "$ZIP" --draft "${GH_FLAGS[@]}"
git push -q origin "$TAG"
gh release edit "$TAG" --draft=false
echo "Published $TAG. The Release workflow commits CHANGELOG.md to master: pull before the next change."
