#!/usr/bin/env python3
"""Count how often something new takes a family leader's movement.

Reads worldserver log lines on stdin (kubectl logs, or a log export with one
message per line) and prints, per leader and per ten-minute window:

  handoffs  the movement-affecting lines of the rules that each steered the
            leader on their own poll before the leader intent book: holds
            placed and lifted, errands sent and released, `new rpg` granted
            and taken, walks back for a member started and stopped, and bot
            commands handed to him that move him.
  intents   the book's own "leader intent '<name>' -> ..." lines, one per
            change of what holds him.

Before the book, `handoffs` is the number of times he was stopped and started;
after it, `intents` is. The target is a handful per ten minutes, not dozens.

Usage:
  kubectl -n <ns> logs deploy/worldserver --since=2h | python3 count.py Grug Uzza
  python3 count.py Grug < exported.log

Pure standard library. Only lines naming a leader in single quotes are read.
"""

from __future__ import annotations

import re
import sys
from collections import defaultdict

# The old rules' own lines, as the module wrote them before the book existed.
# Each is one moment a rule acted on the leader's movement by itself.
HANDOFF_PATTERNS = (
    r"is held still to ",
    r"is released from its .* hold",
    r"had a mover handed back mid-",
    r"sent to '[^']*' - creature",
    r"releasing the errand",
    r"leads but did not carry `new rpg` - granting",
    r"taking `new rpg` off",
    r"goes back for '",
    r"stops going back for '",
    r"handed command \d+ \('(follow|stay|reset ai|nc [^']*)'\)",
    r"takes the wheel from `follow`",
    r"was not carrying `new rpg` while\s+sent",
    r"HOLDS WHERE IT STANDS",
)
HANDOFF = re.compile("|".join(HANDOFF_PATTERNS))
INTENT = re.compile(r"leader intent '(?P<name>[^']+)' -> ")
STAMP = re.compile(r"(?P<date>\d{4}-\d{2}-\d{2})[ T](?P<h>\d{2}):(?P<m>\d{2}):\d{2}")


def window_of(line: str) -> str | None:
    """The ten-minute window a line falls in, as 'YYYY-MM-DD HH:M0', or None."""
    stamp = STAMP.search(line)
    if not stamp:
        return None
    minute = int(stamp["m"]) // 10 * 10
    return f"{stamp['date']} {stamp['h']}:{minute:02d}"


def subject_of(line: str, leaders: list[str]) -> str | None:
    """The leader a line is ABOUT: the first quoted name in its message."""
    message = line.split("overseer:", 1)[-1]
    first = re.search(r"'([^']+)'", message)
    if not first:
        return None
    name = first.group(1)
    return name if name in leaders else None


def count(lines, leaders: list[str]):
    handoffs: dict[tuple[str, str], int] = defaultdict(int)
    intents: dict[tuple[str, str], int] = defaultdict(int)
    for line in lines:
        window = window_of(line)
        if window is None:
            continue
        intent = INTENT.search(line)
        if intent and intent["name"] in leaders:
            intents[(intent["name"], window)] += 1
            continue
        if not HANDOFF.search(line):
            continue
        # "handed command N ('follow') to 'Grug'" names the leader second.
        if "handed command" in line:
            to = re.search(r"\) to '([^']+)'", line)
            name = to.group(1) if to and to.group(1) in leaders else None
        else:
            name = subject_of(line, leaders)
        if name:
            handoffs[(name, window)] += 1
    return handoffs, intents


def main(argv: list[str]) -> int:
    leaders = argv[1:]
    if not leaders:
        print(__doc__.strip().splitlines()[0], file=sys.stderr)
        print("usage: count.py LEADER [LEADER...] < worldserver.log", file=sys.stderr)
        return 2
    handoffs, intents = count(sys.stdin, leaders)
    keys = sorted(set(handoffs) | set(intents), key=lambda k: (k[0], k[1]))
    print("leader\twindow\thandoffs\tintents")
    for name, window in keys:
        print(f"{name}\t{window}\t{handoffs.get((name, window), 0)}\t"
              f"{intents.get((name, window), 0)}")
    for name in leaders:
        mine = [k for k in keys if k[0] == name]
        if not mine:
            continue
        h = sum(handoffs.get(k, 0) for k in mine)
        i = sum(intents.get(k, 0) for k in mine)
        print(f"# {name}: {len(mine)} window(s), {h / len(mine):.1f} handoffs and "
              f"{i / len(mine):.1f} intent changes per 10 minutes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
