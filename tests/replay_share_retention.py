"""Run the shipped share retention and the bridge's read on an isolated MySQL 8.

The acceptance of #1251: the retention keeps exactly what the share backoff
reads. BRIDGE_READ is a copy of the statement in wow-overseer's
`_answered_share_rows`, with its recent-answer filter widened to every triple,
because a triple quiet today may answer tomorrow, and updated_at in place of
the age derived from it, because the clock moves between two reads.
"""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
DATABASE_SQL = (
    "CREATE DATABASE IF NOT EXISTS overseer_share_retention_replay "
    "CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;\n"
    "USE overseer_share_retention_replay;\n"
)
SCHEMA = """
DROP TABLE IF EXISTS overseer_command;
CREATE TABLE overseer_command (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  target_name VARCHAR(12) NOT NULL,
  command VARCHAR(255) NOT NULL,
  kind ENUM('chat','probe','share') NOT NULL DEFAULT 'chat',
  target_arg VARCHAR(255) NOT NULL DEFAULT '',
  status ENUM('pending','claimed','delivered','error') NOT NULL DEFAULT 'pending',
  result TEXT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  KEY idx_target_created (target_name, created_at)
);
"""
BRIDGE_READ = """
SELECT target_name, target_arg, command, status, result, updated_at
FROM (
  SELECT target_name, target_arg, command, status, result, updated_at,
    ROW_NUMBER() OVER (
      PARTITION BY target_name, target_arg, command, status,
        CASE WHEN JSON_VALID(result)
             THEN JSON_UNQUOTE(JSON_EXTRACT(result, '$.reason')) END
      ORDER BY updated_at DESC) AS rn
  FROM overseer_command
  WHERE kind = 'share' AND status IN ('delivered', 'error')
    AND updated_at > NOW() - INTERVAL 90 DAY
) AS answered
WHERE rn <= 8
ORDER BY target_name, target_arg, command, status, updated_at, result;
"""
BRIDGE_SOURCE = (
    "https://raw.githubusercontent.com/quadseven/wow-overseer/main/bridge.py"
)
# What the copy above and the retention both rest on, in the bridge's words.
BRIDGE_FRAGMENTS = (
    "ROW_NUMBER() OVER ( PARTITION BY target_name, target_arg, command, status, "
    "CASE WHEN JSON_VALID(result) "
    "THEN JSON_UNQUOTE(JSON_EXTRACT(result, '$.reason')) END "
    "ORDER BY updated_at DESC) AS rn",
    "WHERE kind = 'share' AND status IN ('delivered', 'error') "
    "AND updated_at > NOW() - INTERVAL %s DAY",
    "WHERE rn <= %s",
)
BRIDGE_DEFAULTS = {
    "SHARE_REFUSAL_MEMORY_DAYS": "90",
    "SHARE_RETRY_MINUTES": "60",  # with the cap below, history_depth() is 8
    "SHARE_BACKOFF_CAP_HOURS": "168",
}
REFUSED = '{"reason":"quest log full"}'
OTHER = '{"reason":"not grouped"}'


def row(name, age_hours, status="error", result=REFUSED, kind="share", arg="Taker"):
    text = "NULL" if result is None else "'%s'" % result
    return (
        "INSERT INTO overseer_command "  # noqa: S608 - fixed test literals, no outside input
        "(target_name, command, kind, target_arg, status, result, updated_at) "
        "VALUES ('%s', 'share 100', '%s', '%s', '%s', %s, "
        "NOW() - INTERVAL %d HOUR);\n" % (name, kind, arg, status, text, age_hours)
    )


class BridgeReadDriftTest(unittest.TestCase):
    """A change to the bridge's read fails here, so the copy cannot wander.
    Only an unreachable source skips: that says nothing about the read."""

    def test_the_copy_still_matches_the_bridge(self):
        try:
            with urllib.request.urlopen(BRIDGE_SOURCE, timeout=30) as reply:  # noqa: S310 - fixed https url
                source = reply.read().decode()
        except urllib.error.HTTPError:
            raise  # the host answered: a moved file is a real failure
        except urllib.error.URLError as error:
            self.skipTest("the bridge source is unreachable: %s" % error)
        start = source.index("def _answered_share_rows")
        body = source[start : source.index("def _insert_share", start)]
        body = body[body.index("readtimeout.timed_execute") :]
        code = "\n".join(line.split("  #")[0] for line in body.splitlines())
        text = " ".join("".join(re.findall(r'"([^"]*)"', code)).split())
        for fragment in BRIDGE_FRAGMENTS:
            self.assertIn(fragment, text)
        for name, value in BRIDGE_DEFAULTS.items():
            self.assertRegex(
                source, r'%s = int\(os\.environ\.get\("%s", "%s"\)\)' % (name, name, value)
            )


class ShareRetentionReplayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        cls.scratch = tempfile.TemporaryDirectory()
        cls.printer = Path(cls.scratch.name) / "print_sql"
        subprocess.run(
            [
                compiler, "-std=c++17", "-I", str(ROOT / "src"),
                str(ROOT / "src/overseer_decisions.cpp"),
                str(ROOT / "tests/fixtures/share_retention/print_sql.cpp"),
                "-o", str(cls.printer),
            ],
            check=True,
            timeout=300,
        )

    @classmethod
    def tearDownClass(cls):
        cls.scratch.cleanup()

    def sql(self, mode, *args):
        return subprocess.run(
            [str(self.printer), mode, *map(str, args)],
            check=True, capture_output=True, text=True,
        ).stdout

    def mysql(self, sql):
        command = json.loads(
            os.environ.get(
                "MYSQL_REPLAY_COMMAND",
                '["mysql","--protocol=TCP","--host=127.0.0.1","--user=root"]',
            )
        )
        result = subprocess.run(  # noqa: S603 - explicitly supplied test database client
            [*command, "--batch", "--skip-column-names"],
            input=DATABASE_SQL + sql,
            capture_output=True,
            text=True,
            timeout=45,
        )
        self.assertEqual(0, result.returncode, result.stderr)
        return result.stdout

    def sweep(self, holder):
        """One holder's sweep, as the module runs it: select, then delete."""
        ids = self.mysql(self.sql("select", holder) + ";").split()
        if ids:
            self.mysql(self.sql("delete", *ids) + ";")
        return len(ids)

    def holders(self):
        found, after = [], ""
        while True:
            holder = self.mysql(self.sql("next", after) + ";").strip()
            if not holder:
                return found
            found.append(holder)
            after = holder

    def drain(self):
        for holder in self.holders():
            while self.sweep(holder):
                pass

    def setUp(self):
        self.mysql(SCHEMA)

    def test_kept_rows_are_exactly_the_bridge_read(self):
        script = ""
        for hour in range(1, 31):  # a streak well past the depth
            script += row("Long", hour * 3)
        for hour in range(1, 6):  # a short streak, all of it kept
            script += row("Short", hour)
        for hour in range(1, 13):  # another reason, status and JSON state
            script += row("Long", hour, result=OTHER)
            script += row("Long", hour, status="delivered", result=None)
            script += row("Long", hour, result="not json")
        script += row("Old", 91 * 24) + row("Old", 89 * 24)  # across the window
        script += row("Tied", 2) * 12  # one second, more rows than the depth
        self.mysql(script)
        before = self.mysql(BRIDGE_READ)

        self.drain()

        self.assertEqual(before, self.mysql(BRIDGE_READ))
        kept = "SELECT COUNT(*) FROM overseer_command;"
        self.assertEqual(len(before.splitlines()), int(self.mysql(kept)))

    def test_a_row_the_read_cannot_see_is_never_counted_as_kept(self):
        # Nine old rows: the youngest eight of the group are still past 90 days.
        self.mysql("".join(row("Old", (91 + n) * 24) for n in range(9)))
        self.drain()
        self.assertEqual("0\n", self.mysql("SELECT COUNT(*) FROM overseer_command;"))

    def test_other_kinds_and_open_rows_are_never_touched(self):
        script = "".join(row("Old", (91 + n) * 24, kind="probe") for n in range(12))
        script += "".join(row("Old", (91 + n) * 24, kind="chat") for n in range(12))
        script += "".join(row("Old", 92 * 24, status="pending") for _ in range(3))
        script += "".join(row("Old", 92 * 24, status="claimed") for _ in range(3))
        self.mysql(script)
        self.drain()
        self.assertEqual("30\n", self.mysql("SELECT COUNT(*) FROM overseer_command;"))

    def test_the_count_matches_and_a_second_pass_deletes_nothing(self):
        self.mysql("".join(row("Long", hour) for hour in range(1, 21)))
        self.assertEqual("12\n", self.mysql(self.sql("count") + ";"))
        self.assertEqual(12, self.sweep("Long"))
        self.assertEqual("0\n", self.mysql(self.sql("count") + ";"))
        self.assertEqual(0, self.sweep("Long"))
        self.assertEqual("8\n", self.mysql("SELECT COUNT(*) FROM overseer_command;"))

    def test_a_sweep_reads_only_its_holder(self):
        self.mysql(
            "".join(row("Aone", h) for h in range(1, 12))
            + "".join(row("Btwo", h) for h in range(1, 12))
        )
        self.assertEqual(["Aone", "Btwo"], self.holders())
        self.assertEqual(3, self.sweep("Aone"))
        self.assertEqual("3\n", self.mysql(self.sql("count") + ";"))

    def test_one_sweep_is_bounded(self):
        values = ",".join(
            "('Long','share 100','share','Taker','error','%s',NOW() - INTERVAL %d MINUTE)"
            % (REFUSED, n)
            for n in range(1, 5021)
        )
        self.mysql(
            "INSERT INTO overseer_command "  # noqa: S608 - fixed test literals, no outside input
            "(target_name, command, kind, target_arg, status, result, updated_at) "
            "VALUES %s;" % values
        )
        self.assertEqual(5000, self.sweep("Long"))
        self.assertEqual(12, self.sweep("Long"))
        self.assertEqual(0, self.sweep("Long"))


if __name__ == "__main__":
    unittest.main()
