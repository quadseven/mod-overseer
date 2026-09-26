#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 TRACE_FILE WORLDSERVER_BINARY" >&2
  exit 2
fi

trace=$1
binary=$2
base=$(awk '/^base 0x/ { print $2; exit }' "$trace")
if [[ -z $base ]]; then
  echo "No load base found in $trace" >&2
  exit 1
fi

awk '
  /^base 0x/ { next }
  match($0, /\[0x[0-9a-fA-F]+\]/) {
    address = substr($0, RSTART + 1, RLENGTH - 2)
    print address
  }
' "$trace" | while IFS= read -r address; do
  address=${address#0x}
  base_value=${base#0x}
  offset=$(printf '%x' "$((16#$address - 16#$base_value))")
  addr2line -f -C -i -e "$binary" "$offset"
done
