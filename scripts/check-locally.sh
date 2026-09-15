#!/usr/bin/env bash
# Run the same two checks that gate a pull request on this repo, locally,
# against the working tree in the current directory.
#
# This mirrors .github/workflows/check.decisions.yml and check.adapter.yml
# exactly (not build.yml, which only runs after a merge to main and takes
# 36-90 minutes -- see that workflow's own comments for why it is not a
# per-PR gate). Both of those two run on every pull request with no path
# filter, so both run here regardless of which files changed.
#
# Usage: scripts/check-locally.sh          (both checks)
#        scripts/check-locally.sh decisions  (fast: seconds, g++ only)
#        scripts/check-locally.sh adapter     (~2 min: needs clang, cmake,
#                                              ninja and the Linux toolchain
#                                              apps/docker/Dockerfile installs
#                                              -- run inside WSL2 on Windows)
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

run_decisions() {
  echo "== decisions: version strings agree"
  local file header
  file="$(tr -d '[:space:]' < VERSION)"
  header="$(grep -oE '^constexpr char VERSION\[\] = "[^"]+"' src/overseer_decisions.h \
    | grep -oE '"[^"]+"' | tr -d '"')"
  test -n "$file" || { echo "VERSION file is empty" >&2; return 1; }
  test -n "$header" || { echo "no VERSION constant in src/overseer_decisions.h" >&2; return 1; }
  test "$file" = "$header" || { echo "version drift: VERSION=$file header=$header" >&2; return 1; }
  printf '%s\n' "$file" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$' || { echo "not semver: $file" >&2; return 1; }

  echo "== decisions: compile and run"
  g++ --version | head -1
  shopt -s nullglob
  local found=0 test_src name
  local workdir
  workdir="$(mktemp -d)"
  trap 'rm -rf "$workdir"' RETURN
  for test_src in tests/test_*.cpp; do
    found=1
    name="$(basename "$test_src" .cpp)"
    echo "-- $test_src"
    g++ -std=c++17 -Wall -Wextra -Isrc src/overseer_decisions.cpp "$test_src" -o "$workdir/$name"
    "$workdir/$name"
  done
  test "$found" = 1 || { echo "no tests/test_*.cpp found" >&2; return 1; }
}

run_adapter() {
  local core_repo="https://github.com/mod-playerbots/azerothcore-wotlk.git"
  local module_repo="https://github.com/mod-playerbots/mod-playerbots.git"
  local core_sha module_sha
  core_sha="$(grep -oE '^[[:space:]]+CORE_SHA:[[:space:]]+[0-9a-f]{40}[[:space:]]*$' \
    .github/workflows/build.yml | head -1 | awk '{print $2}')"
  module_sha="$(grep -oE '^[[:space:]]+MODULE_SHA:[[:space:]]+[0-9a-f]{40}[[:space:]]*$' \
    .github/workflows/build.yml | head -1 | awk '{print $2}')"
  test -n "$core_sha" || { echo "could not read CORE_SHA out of build.yml" >&2; return 1; }
  test -n "$module_sha" || { echo "could not read MODULE_SHA out of build.yml" >&2; return 1; }
  echo "== adapter: pins core $core_sha, mod-playerbots $module_sha"

  for tool in clang++ clang cmake ninja jq; do
    command -v "$tool" >/dev/null || {
      echo "missing $tool -- run this inside the same toolchain apps/docker/Dockerfile installs (WSL2 + build-essential libtool cmake-data ninja-build cmake clang git curl unzip openssl default-libmysqlclient-dev libboost-all-dev libssl-dev libmysql++-dev libreadline-dev zlib1g-dev libbz2-dev libncurses-dev liblzma-dev)" >&2
      return 1
    }
  done

  local work
  work="$(mktemp -d)"
  trap 'rm -rf "$work"' RETURN

  echo "== adapter: laying out the pinned tree"
  git init -q "$work/acore"
  git -C "$work/acore" remote add origin "$core_repo"
  git -C "$work/acore" fetch -q --depth 1 origin "$core_sha"
  git -C "$work/acore" checkout -q FETCH_HEAD

  mkdir -p "$work/acore/modules"
  git init -q "$work/acore/modules/mod-playerbots"
  git -C "$work/acore/modules/mod-playerbots" remote add origin "$module_repo"
  git -C "$work/acore/modules/mod-playerbots" fetch -q --depth 1 origin "$module_sha"
  git -C "$work/acore/modules/mod-playerbots" checkout -q FETCH_HEAD
  rm -rf "$work/acore/modules/mod-playerbots/.git"

  cp -r "$repo_root" "$work/acore/modules/mod-overseer"
  rm -rf "$work/acore/modules/mod-overseer/.git"

  test -f "$work/acore/apps/docker/Dockerfile"
  test -f "$work/acore/modules/mod-overseer/src/mod_overseer.cpp"

  echo "== adapter: applying mod-playerbots patches"
  (cd "$work/acore/modules/mod-overseer" && bash apply-patches.sh "$work/acore")

  echo "== adapter: configuring"
  mkdir -p "$work/acore/build"
  (
    cd "$work/acore/build"
    cmake "$work/acore" \
      -G Ninja \
      -DCMAKE_INSTALL_PREFIX="$PWD/dist" \
      -DAPPS_BUILD="all" \
      -DTOOLS_BUILD="none" \
      -DSCRIPTS="static" \
      -DMODULES="static" \
      -DWITH_WARNINGS="ON" \
      -DCMAKE_BUILD_TYPE="RelWithDebInfo" \
      -DCMAKE_CXX_COMPILER="clang++" \
      -DCMAKE_C_COMPILER="clang" \
      -DBoost_USE_STATIC_LIBS="ON"
    cmake --build . --target revision.h
  )

  echo "== adapter: compiling src/mod_overseer.cpp, and only that"
  local src="$work/acore/modules/mod-overseer/src/mod_overseer.cpp"
  local db="$work/acore/build/compile_commands.json"
  test -f "$db" || { echo "no compile_commands.json - configure did not produce one" >&2; return 1; }

  local count
  count="$(jq --arg f "$src" '[.[] | select(.file == $f)] | length' "$db")"
  test "$count" -ge 1 || {
    echo "compile_commands.json has no entry for $src" >&2
    return 1
  }

  local cmd syntax
  cmd="$(jq -r --arg f "$src" '[.[] | select(.file == $f)][0].command' "$db")"
  syntax="$(printf '%s' "$cmd" \
    | sed -E 's@ -o [^ ]+@@g; s@ -c [^ ]+@@g; s@ -MD@@g; s@ -MT [^ ]+@@g; s@ -MF [^ ]+@@g')"
  case "$syntax" in
    *clang++*) ;;
    *) echo "compile command does not name a compiler: $syntax" >&2; return 1 ;;
  esac
  case "$syntax" in
    *" -o "*|*" -c "*|*" -MF "*)
      echo "failed to strip output flags out of the compile command, refusing to run it" >&2
      return 1 ;;
  esac
  eval "$syntax -fsyntax-only \"$src\""
}

mode="${1:-both}"
case "$mode" in
  decisions) run_decisions ;;
  adapter) run_adapter ;;
  both) run_decisions && run_adapter ;;
  *) echo "usage: $0 [decisions|adapter|both]" >&2; exit 2 ;;
esac
