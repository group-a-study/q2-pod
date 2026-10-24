#!/usr/bin/env bash
# Build Q2 Pod test firmware from a commit of q2-pod and publish it as group-a-study/q2-pod's latest
# release. Network Update on a test build fetches that release, so the Q2 takes it with one press.
#
#   ./test-release.sh [REF]     REF: a branch or commit already pushed to origin (default: main)
#
# The firmware is built here, because GitHub can't build it: the stock V1.32 ZIP it patches isn't
# public. Both update.tar files, their notes, this script and the release workflow go in a single
# commit on the branch test-firmware, force-pushed each time so the tars don't pile up in history.
# The workflow there publishes them. Nothing fork-only is committed to main or to a PR branch.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
POD=$HERE/q2-pod    # the clone: origin is the fork, upstream DiamondBond's repo
TC=$HERE/toolchain  # clang/lld 23 and Python 3.12 from conda-forge, and the stock ZIP
ZIP="$TC/Q2 Firmware V1.32.zip"
FORK=group-a-study/q2-pod
export PATH="$TC/llvm/bin:$TC/py312/bin:$PATH"

ref=${1:-main}
git -C "$POD" fetch -q origin
git -C "$POD" fetch -q upstream
src=$(git -C "$POD" rev-parse --verify "$ref^{commit}")
git -C "$POD" branch -r --contains "$src" | grep -q '^ *origin/' ||
  { echo "$ref ($src) isn't on origin: push it first, so the release can point at it" >&2; exit 1; }

work=$(mktemp -d "${TMPDIR:-/tmp}/q2-test-release.XXXXXX")
cleanup() { git -C "$POD" worktree remove --force "$work/src" 2>/dev/null || true; rm -rf "$work"; }
trap cleanup EXIT
git -C "$POD" worktree add -q --detach "$work/src" "$src"
cd "$work/src"

# Point Network Update at this fork: with build.py's --update-repo once the source has it, otherwise
# by changing the URL in this throwaway checkout only.
if grep -q -- '--update-repo' tools/build.py; then
  repo=(--update-repo "$FORK")
else
  repo=()
  sed -i "s|DiamondBond/q2-pod/releases|$FORK/releases|" patch/netupdate.c test/patch.py
  grep -q "\"https://github.com/$FORK/releases/latest\"" patch/netupdate.c ||
    { echo "patch/netupdate.c: Network Update's URL not found" >&2; exit 1; }
fi

# --dev numbers each build from .build-number; the clone keeps it between runs (it's untracked).
counter=$POD/.build-number
for edition in ipod legacy; do
  if [ -f "$counter" ]; then cp "$counter" .build-number; fi
  flags=(--dev "${repo[@]}")
  if [ $edition = ipod ]; then flags+=(--ipod); fi
  for step in build test-build test-patch; do
    echo "== $edition: $step"
    case $step in
      build) cmd=(python3 tools/build.py "$ZIP" --out "$work/$edition" "${flags[@]}") ;;
      test-build) cmd=(python3 test/build.py --build "$work/$edition") ;;
      test-patch) cmd=(python3 test/patch.py "$work/$edition") ;;
    esac
    "${cmd[@]}" > "$work/$edition-$step.log" 2>&1 || { tail -30 "$work/$edition-$step.log"; exit 1; }
    if [ $step = build ]; then cp .build-number "$counter"; fi
  done
done

manifest() { python3 -c 'import json, sys; m = json.load(open(sys.argv[1])); print(m["version"], m["dev"])' "$1/manifest.json"; }
read -r ipod_tag ipod_n < <(manifest "$work/ipod")
read -r legacy_tag legacy_n < <(manifest "$work/legacy")
version=$(sed -n "s/^VERSION = '\([^']*\)'.*/\1/p" tools/build.py)
tag=test-$version-dev$ipod_n
short=$(git rev-parse --short "$src")

tf=$work/tf
mkdir -p "$tf/.github/workflows"
cp "$work/ipod/update.tar" "$tf/update.tar"
cp "$work/legacy/update.tar" "$tf/update-legacy.tar"
(cd "$tf" && sha256sum update.tar update-legacy.tar > SHA256SUMS)
echo "$src" > "$tf/SOURCE"
echo "$tag" > "$tf/TAG"
cp "$HERE/test-release.sh" "$tf/test-release.sh"
cat > "$tf/.github/workflows/test-firmware.yml" <<'YML'
name: Test firmware release
# A push to this branch publishes its two update.tar files as the fork's latest release, which a test
# build's Network Update fetches. test-release.sh builds them locally: the stock ZIP isn't public.
on:
  push:
    branches: [test-firmware]

permissions:
  contents: write

jobs:
  release:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sha256sum -c SHA256SUMS
      - env:
          GH_TOKEN: ${{ github.token }}
        run: >
          gh release create "$(cat TAG)" update.tar update-legacy.tar SHA256SUMS
          -R "$GITHUB_REPOSITORY" --target "$(cat SOURCE)" --latest
          --title "Q2 Pod test build $(cat TAG)" --notes-file NOTES.md
YML

commits=$(git log --format='- %s (%h)' "upstream/main..$src")
{
  echo "Test build of Q2 Pod $version from \`$ref\` ($short), built $(date +%F). Not an official release; those are at https://github.com/DiamondBond/q2-pod/releases."
  echo
  echo "- **update.tar**: iPod edition, $ipod_tag (About shows $version Pod dev $ipod_n)"
  echo "- **update-legacy.tar**: Legacy edition, $legacy_tag (About shows $version dev $legacy_n). Rename it to \`update.tar\` before you copy it to the card."
  echo
  echo "**Install:** on a Q2 already running a test build from this fork, **System settings → System Update → Network Update**. Otherwise copy \`update.tar\` to the root of the card, then **TF card update**. Each test build installs over any other build."
  echo
  echo "Network Update in these builds fetches this fork's releases ($FORK), not DiamondBond's."
  if [ -n "$commits" ]; then
    echo
    echo "**Changes not yet in DiamondBond/q2-pod main:**"
    echo "$commits"
  fi
  echo
  echo '**SHA-256:**'
  echo '```'
  cat "$tf/SHA256SUMS"
  echo '```'
} > "$tf/NOTES.md"

# A one-commit repo of its own, so the tars never enter the clone's object store.
git -C "$tf" init -q
git -C "$tf" symbolic-ref HEAD refs/heads/test-firmware
git -C "$tf" add -A -f
git -C "$tf" -c user.name="$(git -C "$POD" config user.name)" -c user.email="$(git -C "$POD" config user.email)" \
  commit -q -m "Test firmware $tag: $ipod_tag and $legacy_tag, from $src"
git -C "$tf" push -q -f "$(git -C "$POD" remote get-url origin)" test-firmware

echo "Pushed $tag ($ipod_tag / $legacy_tag, from $ref $short)."
echo "In a minute or two it's https://github.com/$FORK/releases/tag/$tag"
echo "Then on the Q2: System settings → System Update → Network Update."
