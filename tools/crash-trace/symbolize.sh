#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 TRACE_FILE WORLDSERVER_BINARY" >&2
  exit 2
fi

trace=$1
binary=$2

if ! command -v addr2line >/dev/null 2>&1; then
  echo "addr2line not found in PATH - it ships with binutils, and resolving a trace with it is all this script does" >&2
  exit 1
fi

base=$(awk '/^base 0x/ { print $2; exit }' "$trace")
if [[ -z $base ]]; then
  echo "No load base found in $trace" >&2
  exit 1
fi
# The base goes into $((16#...)) below, and arithmetic expansion evaluates
# what it is given, so the value out of the trace has to be proved hex
# before it gets there - the frame addresses already get that proof from
# the regex that reads them.
if [[ ! $base =~ ^0x[0-9a-fA-F]+$ ]]; then
  echo "Load base in $trace is not a hex address: $base" >&2
  exit 1
fi
base_value=${base#0x}

mapfile -t addresses < <(awk '
  /^base 0x/ { next }
  match($0, /\[0x[0-9a-fA-F]+\]/) {
    address = substr($0, RSTART + 1, RLENGTH - 2)
    print address
  }
' "$trace")
if [[ ${#addresses[@]} -eq 0 ]]; then
  echo "No frame addresses found in $trace" >&2
  exit 1
fi

for address in "${addresses[@]}"; do
  address=${address#0x}
  offset=$(printf '%x' "$((16#$address - 16#$base_value))")
  addr2line -f -C -i -e "$binary" "$offset"
done
