#!/bin/sh
# Makes the update package that Tools > Update installs, and (with --publish) a GitHub release for it.
#   ./release.sh <package folder> [tag] [--publish]
# The package folder is a test-package (install.sh, SHA256SUMS, .system, Tools, Emus). The tag defaults to
# vYYYY.MM.DD.HHMM (the Update tool compares the numbers in it, so a later build has a bigger tag).
# Output goes in ./release-out: MinUIPlus-<tag>.tar.gz and MinUIPlus-<tag>.tar.gz.sha256.
set -e
src=${1:?usage: release.sh <package folder> [tag] [--publish]}
tag=${2:-v$(date +%Y.%m.%d.%H%M)}
publish=
for a in "$@"; do [ "$a" = "--publish" ] && publish=1; done
[ "$tag" = "--publish" ] && tag=v$(date +%Y.%m.%d.%H%M)
[ -f "$src/install.sh" ] && [ -f "$src/SHA256SUMS" ] || { echo "$src is not a package (no install.sh / SHA256SUMS)"; exit 1; }
(cd "$src" && shasum -a 256 -c SHA256SUMS >/dev/null) || { echo "SHA256SUMS does not match the files in $src"; exit 1; }
out=$(pwd)/release-out
stage=$(mktemp -d)
mkdir -p "$out"
(cd "$src" && tar -cf - --exclude=.DS_Store --exclude='._*' .system Tools Emus install.sh revert.sh SHA256SUMS README.txt 2>/dev/null) | (cd "$stage" && tar -xf -)
printf '%s\n' "$tag" > "$stage/VERSION"
name=MinUIPlus-$tag.tar.gz
COPYFILE_DISABLE=1 tar --format ustar -czf "$out/$name" -C "$stage" .
(cd "$out" && shasum -a 256 "$name" > "$name.sha256")
rm -r "$stage"
echo "made $out/$name ($(wc -c < "$out/$name") bytes)"
cat "$out/$name.sha256"
if [ -n "$publish" ]; then
	gh release create "$tag" "$out/$name" "$out/$name.sha256" --title "$tag" --notes "Install with Tools > Update, or run install.sh from the package on a card." --repo "${REPO:-casualducko/MinUIPlus}"
else
	echo "(not published: add --publish to create the GitHub release)"
fi
