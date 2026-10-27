#!/bin/sh
# Makes the update package that Tools > Update installs, and (with --publish) a GitHub release for it.
#   ./release.sh <package folder> [tag] [--publish]
# The package folder is a test-package (SHA256SUMS, .system, Tools, Emus, Roms/Saves/Bios, examples; ./stage-extras.sh fills Emus and the three folders); install.sh, revert.sh and README.txt
# are always taken from this folder (tools/package), so the package never carries an old copy. The tag defaults to
# vYYYY.MM.DD.HHMM (the Update tool compares the numbers in it, so a later build has a bigger tag).
# Output goes in ./release-out: MinUIPlus-<tag>.tar.gz and MinUIPlus-<tag>.tar.gz.sha256.
set -e
src=${1:?usage: release.sh <package folder> [tag] [--publish]}
tag=${2:-v$(date +%Y.%m.%d.%H%M)}
publish=
for a in "$@"; do [ "$a" = "--publish" ] && publish=1; done
[ "$tag" = "--publish" ] && tag=v$(date +%Y.%m.%d.%H%M)
here=$(cd "$(dirname "$0")" && pwd)
[ -f "$src/SHA256SUMS" ] && [ -d "$src/.system" ] && [ -d "$src/Tools" ] || { echo "$src is not a package (no SHA256SUMS / .system / Tools)"; exit 1; }
for f in install.sh revert.sh README.txt; do [ -f "$here/$f" ] || { echo "missing $here/$f"; exit 1; }; done
(cd "$src" && shasum -a 256 -c SHA256SUMS >/dev/null) || { echo "SHA256SUMS does not match the files in $src"; exit 1; }
out=$(pwd)/release-out
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$out"
parts=".system Tools Emus Roms Saves Bios SHA256SUMS"
[ -d "$src/examples" ] && parts="$parts examples"
(cd "$src" && tar -cf - --exclude=.DS_Store --exclude='._*' $parts) | (cd "$stage" && tar -xf -)
cp "$here/install.sh" "$here/revert.sh" "$here/README.txt" "$stage/"
printf '%s\n' "$tag" > "$stage/VERSION"
name=MinUIPlus-$tag.tar.gz
COPYFILE_DISABLE=1 tar --format ustar -czf "$out/$name" -C "$stage" .
(cd "$out" && shasum -a 256 "$name" > "$name.sha256")
# the web installer (docs/index.html) can't download release files from GitHub (CORS), so it reads its own copy
pages=$(cd "$(dirname "$0")/../.." && pwd)/docs/latest
if [ -n "$publish" ] && [ -z "$NO_PAGES" ] && [ -d "$(dirname "$pages")" ]; then # only for a published release (NO_PAGES=1: not even then)
	rm -rf "$pages"; mkdir -p "$pages"
	cp "$out/$name" "$pages/$name"
	printf '{"tag":"%s","file":"%s","sha256":"%s","size":%s}\n' "$tag" "$name" "$(cut -d' ' -f1 < "$out/$name.sha256")" "$(wc -c < "$out/$name" | tr -d ' ')" > "$pages/version.json"
	echo "web installer copy: $pages (commit and push it so the page offers this build)"
fi
echo "made $out/$name ($(wc -c < "$out/$name") bytes)"
cat "$out/$name.sha256"
if [ -n "$publish" ]; then
	gh release create "$tag" "$out/$name" "$out/$name.sha256" --title "$tag" --notes "Install with Tools > Update, or run install.sh from the package on a card." --repo "${REPO:-casualducko/MinUIPlus}"
else
	echo "(not published: add --publish to create the GitHub release)"
fi
