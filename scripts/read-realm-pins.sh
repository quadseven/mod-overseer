#!/usr/bin/env bash
#
# Read and validate realm-pins.env, and print KEY=value lines for a workflow to
# append to its environment.
#
#   read-realm-pins.sh <pins-file> [HEADS_]
#
# With no second argument the CORE_* and MODULE_* keys are printed as they are
# named in the file. With HEADS_ the HEADS_CORE_* and HEADS_MODULE_* keys are
# printed under the unprefixed names, so a caller does not care which set it
# asked for. The other modules' keys are printed either way.
#
# There is no fallback. A missing file, an absent key, an empty value, a repo
# that is not an https URL or a SHA that is not 40 lowercase hex characters is
# an error that names the file, and nothing is printed: a gate that guessed at
# a pin would compile a tree the realm does not run.
set -euo pipefail

file="${1:-}"
prefix="${2:-}"

fail() {
  if [ -n "${GITHUB_ACTIONS:-}" ]; then
    echo "::error::$*" >&2
  else
    echo "error: $*" >&2
  fi
  exit 1
}

test -n "$file" || fail "usage: read-realm-pins.sh <pins-file> [HEADS_]"
case "$prefix" in
  "" | HEADS_) ;;
  *) fail "the prefix must be empty or HEADS_, got: $prefix" ;;
esac
test -f "$file" || fail "$file is missing; the adapter check and the build compile against the pins it declares and will not guess at them"

get() {
  local value
  value="$(grep -E "^$1=" "$file" | head -1 | cut -d= -f2- | tr -d '\r' || true)"
  printf '%s' "$value"
}

out=""
check() { # key, kind
  local value
  value="$(get "$1")"
  test -n "$value" || fail "$file: $1 is absent or empty"
  case "$2" in
    sha)
      printf '%s' "$value" | grep -qE '^[0-9a-f]{40}$' \
        || fail "$file: $1=$value is not a 40-character lowercase hex SHA"
      ;;
    repo)
      printf '%s' "$value" | grep -qE '^https://[^[:space:]]+$' \
        || fail "$file: $1=$value is not an https URL"
      ;;
  esac
  printf '%s' "$value"
}

# Every key is validated whichever set was asked for, so a typo in the set not
# being compiled today still fails today and not at the next roll.
for set in "" HEADS_; do
  for key in CORE MODULE; do
    check "${set}${key}_REPO" repo >/dev/null
    check "${set}${key}_SHA" sha >/dev/null
  done
done
for key in OLLAMA_CHAT DUNGEON_CLEAR JUNK_TO_GOLD AH_BOT; do
  check "${key}_REPO" repo >/dev/null
  check "${key}_SHA" sha >/dev/null
done

for key in CORE MODULE; do
  out+="${key}_REPO=$(get "${prefix}${key}_REPO")"$'\n'
  out+="${key}_SHA=$(get "${prefix}${key}_SHA")"$'\n'
done
for key in OLLAMA_CHAT DUNGEON_CLEAR JUNK_TO_GOLD AH_BOT; do
  out+="${key}_REPO=$(get "${key}_REPO")"$'\n'
  out+="${key}_SHA=$(get "${key}_SHA")"$'\n'
done
printf '%s' "$out"
