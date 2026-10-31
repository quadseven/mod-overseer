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
#
# A STOCK UBUNTU IMAGE CAN STILL FAIL THE ADAPTER CHECK WITH A LINK ERROR ON
# EVERY C++ FILE, NOT JUST THIS MODULE'S (seen on a WSL2 Ubuntu 26.04 image,
# 2026-09-15): clang++ auto-detects whichever gcc version's directory is
# newest on the system and prefers its libstdc++, but the apt list above only
# guarantees THAT gcc's own package is present, not its libstdc++-dev -- if
# something else on the box pulled in a newer gcc's runtime first, clang++
# picks it and then has no matching libstdc++ headers/libs to link against.
# `clang++ --version` and a trivial `echo 'int main(){}' | clang++ -x c++ - -o
# /tmp/t` will fail the same way before this script ever gets involved; the
# fix is installing that specific gcc version's libstdc++-<N>-dev package,
# not anything in this script or this repository.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

# Every scratch folder this run makes goes on this list and is removed when
# the script exits, however it exits. A RETURN trap alone never fires when
# set -e ends the script mid-function (a failed compile or test), and those
# runs left a build folder of several hundred MB in TMPDIR each time.
scratch_dirs=()
cleanup_scratch() { [ "${#scratch_dirs[@]}" -eq 0 ] || rm -rf "${scratch_dirs[@]}"; }
trap cleanup_scratch EXIT

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

  python3 -B tests/replay_exit_hearth_finalization.py
  python3 -B tests/replay_travel_claim_cache.py
  python3 -B tests/replay_main_tank_cache.py
  python3 -B tests/replay_door_floor.py

  echo "== decisions: compile and run"
  g++ --version | head -1
  shopt -s nullglob
  local found=0 test_src name
  local workdir
  workdir="$(mktemp -d)"
  scratch_dirs+=("$workdir")
  # Compiled once and linked into every test, as check.decisions.yml does.
  g++ -std=c++17 -Wall -Wextra -Isrc -c src/overseer_decisions.cpp -o "$workdir/overseer_decisions.o"
  for test_src in tests/test_*.cpp; do
    found=1
    name="$(basename "$test_src" .cpp)"
    echo "-- $test_src"
    g++ -std=c++17 -Wall -Wextra -Isrc "$workdir/overseer_decisions.o" "$test_src" -o "$workdir/$name"
    "$workdir/$name"
  done
  test "$found" = 1 || { echo "no tests/test_*.cpp found" >&2; return 1; }
}

run_adapter() {
  local core_repo module_repo core_sha module_sha pins
  # realm-pins.env is the one copy of the pins (#574); the reader fails,
  # naming the file, rather than guessing.
  pins="$(bash scripts/read-realm-pins.sh realm-pins.env)" || return 1
  pin() { printf '%s\n' "$pins" | grep -E "^$1=" | head -1 | cut -d= -f2-; }
  core_repo="$(pin CORE_REPO)"
  module_repo="$(pin MODULE_REPO)"
  core_sha="$(pin CORE_SHA)"
  module_sha="$(pin MODULE_SHA)"
  echo "== adapter: pins core $core_sha, mod-playerbots $module_sha"

  for tool in clang++ clang cmake ninja jq; do
    command -v "$tool" >/dev/null || {
      echo "missing $tool -- run this inside the same toolchain apps/docker/Dockerfile installs (WSL2 + build-essential libtool cmake-data ninja-build cmake clang git curl unzip openssl default-libmysqlclient-dev libboost-all-dev libssl-dev libmysql++-dev libreadline-dev zlib1g-dev libbz2-dev libncurses-dev liblzma-dev)" >&2
      return 1
    }
  done

  local work
  work="$(mktemp -d)"
  scratch_dirs+=("$work")

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

  local name key
  for name in mod-ollama-chat:OLLAMA_CHAT mod-dungeon-clear:DUNGEON_CLEAR \
              mod-junk-to-gold:JUNK_TO_GOLD mod-ah-bot-plus:AH_BOT; do
    key="${name#*:}"; name="${name%%:*}"
    git init -q "$work/acore/modules/$name"
    git -C "$work/acore/modules/$name" remote add origin "$(pin "${key}_REPO")"
    git -C "$work/acore/modules/$name" fetch -q --depth 1 origin "$(pin "${key}_SHA")"
    git -C "$work/acore/modules/$name" checkout -q FETCH_HEAD
    rm -rf "$work/acore/modules/$name/.git"
  done

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

  # $syntax comes from compile_commands.json, which cmake generates from this
  # checkout's own build files -- not something to hand to eval or any other
  # shell re-parse. xargs's word-splitting understands quoting and escaping
  # but, unlike a shell, never expands $(...), backticks, ;, | or & -- so a
  # malicious flag value lands as one inert argv element instead of running.
  local -a args
  mapfile -t args < <(xargs -n1 <<<"$syntax")
  "${args[@]}" -fsyntax-only "$src"
}

mode="${1:-both}"
case "$mode" in
  decisions) run_decisions ;;
  adapter) run_adapter ;;
  both) run_decisions && run_adapter ;;
  *) echo "usage: $0 [decisions|adapter|both]" >&2; exit 2 ;;
esac
