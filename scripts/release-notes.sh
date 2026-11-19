#!/usr/bin/env bash
# Writes the release notes and CHANGELOG.md of a version: scripts/release-notes.sh <tag> <zip name> <output folder>
#   notes.md       the README's header, a summary (Atmosphère, tested firmware, download, install), the changes since the
#                  previous release (git-cliff, cliff.toml) and the contributors
#   CHANGELOG.md   the README's header and every release
# Works before the tag exists (scripts/release.sh) and after it was pushed (the Release workflow). With GITHUB_TOKEN set,
# authors show as GitHub users and pull requests are linked.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

TAG="$1"
ZIP_NAME="$2"
OUT="$3"
mkdir -p "$OUT"

PRERELEASE=false
[[ "$TAG" == v0.* || "$TAG" == *-* ]] && PRERELEASE=true
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    RANGE=(--latest)
else
    RANGE=(--unreleased --tag "$TAG")
fi

FORK_SHA="$(git ls-tree HEAD atmosphere | awk '{print $3}')"
TESTED="$(grep -o 'tested%20on-[0-9.]*' README.md | head -n1 | cut -d- -f2)"

# The README's header (icon, title, badges, links): with links into the README for CHANGELOG.md, and with full GitHub
# addresses of the tagged files for the notes, since the release page resolves no relative path.
readme_header() {
    python3 - "$1" "$TAG" <<'HEADER'
import re, sys
target, tag = sys.argv[1], sys.argv[2]
readme = open("README.md").read()
header = readme[:readme.index("</div>") + len("</div>")]
if target == "changelog":
    header = re.sub(r"\]\(#", "](README.md#", header)
    header = header.replace("[Changelog](CHANGELOG.md)", "[README](README.md)")
else:
    base = f"https://github.com/NedcloarBR/USB-Loader-NX/blob/{tag}/"
    header = header.replace('src="assets/icon.svg"', f'src="{base}assets/icon.svg?raw=true"')
    header = re.sub(r"\]\(#", f"]({base}README.md#", header)
    header = re.sub(r"\]\((?!https?:)([^)#]+)\)", lambda m: f"]({base}{m.group(1)})", header)
print(header)
print()
HEADER
}

# Everyone with a commit in this release: GitHub user when git-cliff could look it up, else the git author.
CONTRIBUTORS="$(git-cliff "${RANGE[@]}" --context 2>/dev/null | python3 -c '
import json, sys
seen = []
for release in json.load(sys.stdin):
    for commit in release["commits"]:
        name = (commit.get("remote") or {}).get("username")
        name = "@" + name if name else commit["author"]["name"]
        if name not in seen:
            seen.append(name)
print(", ".join(seen))')"

{
    readme_header notes
    if $PRERELEASE; then
        echo "> [!WARNING]"
        echo "> Pre-release: tested on one console so far. Back up your SD card before installing, and report what you find."
        echo
    fi
    echo "| | |"
    echo "|---|---|"
    echo "| **Atmosphère** | $(cat ATMOSPHERE_VERSION) (fork [\`${FORK_SHA:0:9}\`](https://github.com/NedcloarBR/USB-Loader-NX/commit/$FORK_SHA)) |"
    [[ -n "$TESTED" ]] && echo "| **Tested on** | Firmware $TESTED |"
    echo "| **Download** | \`$ZIP_NAME\` below |"
    echo "| **Install** | Unzip, copy \`SD\` to the SD card and \`USB\` to the USB drive; the \`README.md\` inside has the details |"
    echo
    # The release page already shows the version: the notes start at the first group.
    git-cliff "${RANGE[@]}" --strip header 2>/dev/null | python3 -c 'import sys; text = sys.stdin.read().strip(); print(text[text.index("###"):] if "###" in text else text)'
    echo
    echo "**Contributors:** $CONTRIBUTORS"
} > "$OUT/notes.md"

{
    readme_header changelog
    git-cliff --tag "$TAG" 2>/dev/null
} > "$OUT/CHANGELOG.md"
