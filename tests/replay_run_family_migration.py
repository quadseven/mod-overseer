"""Apply the real ledger migrations twice against an isolated MySQL 8 database."""

import json
import os
from pathlib import Path
import subprocess
import unittest

# The workflow runs this file on its MySQL service, so importing the class runs it.
from replay_share_retention import (  # noqa: F401
    BridgeReadDriftTest,
    ShareRetentionReplayTest,
)

ROOT = Path(__file__).resolve().parents[1]
SQL = ROOT / "data/sql/characters/base"
DATABASE_SQL = (
    "CREATE DATABASE IF NOT EXISTS overseer_run_family_replay "
    "CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;\n"
    "USE overseer_run_family_replay;\n"
)


class RunFamilyMigrationTest(unittest.TestCase):
    def test_zone_danger_snapshot_migration_is_idempotent_and_preserves_json(self):
        migration = (SQL / "2026_11_01_00_overseer_zone_danger.sql").read_text()
        self.mysql("DROP TABLE IF EXISTS overseer_zone_danger;" + migration + migration)
        self.mysql("""
REPLACE INTO overseer_zone_danger(name,result) VALUES ('LeaderA','{"zones":[]}');
REPLACE INTO overseer_zone_danger(name,result) VALUES ('LeaderA','{"zones":[{"zone_id":46,"score":3}]}');
""")
        result = self.mysql(
            "SELECT name,JSON_VALID(result),JSON_EXTRACT(result,'$.zones[0].score') "
            "FROM overseer_zone_danger;"
        )
        self.assertEqual("LeaderA\t1\t3", result.strip())
        collation = self.mysql(
            "SELECT TABLE_COLLATION FROM information_schema.TABLES "
            "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='overseer_zone_danger';"
        )
        self.assertEqual("utf8mb4_unicode_ci", collation.strip())

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

    def test_legacy_adoption_is_conservative_and_replay_is_idempotent(self):
        bootstrap = """
DROP TABLE IF EXISTS overseer_dungeon_run;
DROP TABLE IF EXISTS overseer_roster;
CREATE TABLE overseer_roster (
 name VARCHAR(24) PRIMARY KEY, family VARCHAR(32), enabled TINYINT NOT NULL DEFAULT 1
) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
INSERT INTO overseer_roster(name,family) VALUES
 ('LeaderA','FamilyA'),('MemberA','FamilyA'),('LeaderB','FamilyB');
"""
        bootstrap += (SQL / "2026_08_30_00_overseer_dungeon_run.sql").read_text()
        bootstrap += (
            SQL / "2026_09_02_02_overseer_dungeon_run_accounting.sql"
        ).read_text()
        bootstrap += """
INSERT INTO overseer_dungeon_run(id,leader_name,map_id,state,members) VALUES
 (1,'LeaderB',129,'active','LeaderA,MemberA'),
 (2,'LeaderA',70,'active','LeaderA,LeaderB'),
 (3,'LeaderA',33,'active','LeaderA,Unknown'),
 (4,'LeaderA',34,'active',''),
 (5,'LeaderA',36,'ended','LeaderA,MemberA'),
 (6,'LeaderB',389,'active','LeaderB');
"""
        self.mysql(bootstrap)
        migration = (SQL / "2026_10_31_00_overseer_dungeon_run_family.sql").read_text()
        self.mysql(migration + migration)
        rows = self.mysql(
            "SELECT id,CONCAT('[',family,']') FROM overseer_dungeon_run ORDER BY id;"
        )
        self.assertEqual(
            ["1\t[FamilyA]", "2\t[]", "3\t[]", "4\t[]", "5\t[]", "6\t[FamilyB]"],
            rows.splitlines(),
        )
        indexes = self.mysql("""SELECT INDEX_NAME,
GROUP_CONCAT(COLUMN_NAME ORDER BY SEQ_IN_INDEX)
FROM information_schema.STATISTICS WHERE TABLE_SCHEMA=DATABASE()
 AND TABLE_NAME='overseer_dungeon_run' AND NON_UNIQUE=0
GROUP BY INDEX_NAME ORDER BY INDEX_NAME;""")
        self.assertEqual(
            ["PRIMARY\tid", "uniq_active_family_map\tfamily,active_map"],
            indexes.splitlines(),
        )
        self.mysql("""
INSERT INTO overseer_dungeon_run(family,leader_name,map_id,state)
 VALUES ('FamilyB','LeaderB',129,'active');
INSERT INTO overseer_dungeon_run(family,leader_name,map_id,state)
 VALUES ('FamilyA','MemberA',129,'active')
 ON DUPLICATE KEY UPDATE last_progress_at=NOW();
""")
        count = self.mysql(
            "SELECT COUNT(*) FROM overseer_dungeon_run "
            "WHERE map_id=129 AND state='active';"
        )
        self.assertEqual("2", count.strip())


class WholeDirectoryMigrationTest(unittest.TestCase):
    """Every base migration applies cleanly again against the full schema.

    dbimport re-applies any file whose hash changed, so a comment-only edit
    re-runs a file against a database that already holds every later
    migration and live rows. A second (and third, and out-of-order) pass
    must be a no-op: no duplicate-column aborts, no dropped tables, and no
    ENUM restated shorter than the final list while rows hold newer values.
    """

    FINAL_KIND = (
        "enum('bot','chat','gm','probe','give','share','trade','job','sell',"
        "'bank','auction','mail','repair','buy','bind','hearth','summon',"
        "'conjure','cast','guild','quest','naturalize','disenchant')"
    )
    FINAL_STATUS = (
        "enum('pending','claimed','delivered','error','verifying','applied',"
        "'unchanged')"
    )

    def run_sql(self, sql, label):
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
        self.assertEqual(0, result.returncode, f"{label}: {result.stderr}")
        return result.stdout

    def apply_directory(self, label):
        # One client call per pass: a process per file costs more than the
        # gate has to spare. Every file's statements are self-contained
        # (the guards set and consume their own session variables), so a
        # shared session changes nothing. If a pass ever fails, replay it
        # file by file so the failure names its file before failing again.
        paths = sorted(SQL.glob("*.sql"))
        payload = "\n".join(path.read_text() for path in paths)
        try:
            self.run_sql(payload, label)
        except AssertionError:
            for path in paths:
                self.run_sql(path.read_text(), f"{label} (isolating): {path.name}")
            raise

    def column_type(self, table, column):
        out = self.run_sql(
            "SELECT COLUMN_TYPE FROM information_schema.COLUMNS "
            f"WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='{table}' "
            f"AND COLUMN_NAME='{column}';",
            f"column type of {table}.{column}",
        )
        return out.strip()

    def test_full_directory_reapplies_cleanly_and_preserves_live_rows(self):
        self.run_sql(
            "DROP DATABASE IF EXISTS overseer_run_family_replay;",
            "reset replay database",
        )
        self.apply_directory("first pass")
        # Live rows a re-apply must never lose or strand: roster members,
        # and a command holding the newest kind and a status the chat
        # migration's own shorter restatement predates.
        self.run_sql(
            "INSERT INTO overseer_roster(name) VALUES ('LeaderA'),('MemberA');",
            "seed roster rows",
        )
        self.run_sql(
            "INSERT INTO overseer_command(target_name,command,kind,status) "
            "VALUES ('LeaderA','say hi','disenchant','verifying');",
            "seed command row",
        )
        # The chat migration ships no watch row (#1344): the watch list is an
        # outside write, and a row already written must outlive a re-apply.
        self.assertEqual(
            "0",
            self.run_sql(
                "SELECT COUNT(*) FROM overseer_chat_watch;",
                "watch list after first pass",
            ).strip(),
        )
        self.run_sql(
            "INSERT INTO overseer_chat_watch(name) VALUES ('Watched');",
            "seed watch row",
        )
        self.apply_directory("second pass")
        self.apply_directory("third pass")
        roster = self.run_sql(
            "SELECT name FROM overseer_roster "
            "WHERE name IN ('LeaderA','MemberA') ORDER BY name;",
            "roster rows after re-apply",
        )
        self.assertEqual(["LeaderA", "MemberA"], roster.splitlines())
        watch = self.run_sql(
            "SELECT name FROM overseer_chat_watch ORDER BY name;",
            "watch rows after re-apply",
        )
        self.assertEqual(["Watched"], watch.splitlines())
        command = self.run_sql(
            "SELECT kind,status FROM overseer_command "
            "WHERE target_name='LeaderA';",
            "command row after re-apply",
        )
        self.assertEqual("disenchant\tverifying", command.strip())
        self.assertEqual(
            self.FINAL_KIND, self.column_type("overseer_command", "kind")
        )
        self.assertEqual(
            self.FINAL_STATUS, self.column_type("overseer_command", "status")
        )
        # dbimport re-applies one changed file at a time, out of order: the
        # two files whose restatements predate the final lists must not
        # shrink either ENUM while the probe rows above are live.
        for name in (
            "2026_08_22_00_overseer_chat.sql",
            "2026_08_24_00_overseer_give.sql",
        ):
            self.run_sql((SQL / name).read_text(), f"out-of-order: {name}")
        self.assertEqual(
            self.FINAL_KIND, self.column_type("overseer_command", "kind")
        )
        self.assertEqual(
            self.FINAL_STATUS, self.column_type("overseer_command", "status")
        )
        command = self.run_sql(
            "SELECT kind,status FROM overseer_command "
            "WHERE target_name='LeaderA';",
            "command row after out-of-order re-apply",
        )
        self.assertEqual("disenchant\tverifying", command.strip())

    def test_disenchant_store_reads_run_against_the_replayed_schema(self):
        # overseer_command takes the server default collation (utf8mb4_0900_ai_ci
        # on MySQL 8) while overseer_roster names utf8mb4_unicode_ci, so a bare
        # join on the two name columns fails to prepare live with "Illegal mix
        # of collations" and the disenchant queue never runs. The commands
        # below spell the name differently from the roster: the join matches
        # case-insensitively, so both reads must hand back the roster's own
        # spelling, the name every later lookup and claim keys on.
        self.run_sql(
            "DROP DATABASE IF EXISTS overseer_run_family_replay;",
            "reset replay database",
        )
        self.apply_directory("schema for store reads")
        self.run_sql(
            "INSERT INTO overseer_roster(name,enabled,job) "
            "VALUES ('CrafterA',1,'craft');"
            "INSERT INTO overseer_command(target_name,command,kind,status,claimed_by) "
            "VALUES ('craftera','item 1','disenchant','pending',''),"
            "('CRAFTERA','item 2','disenchant','claimed','CrafterA');",
            "seed disenchant rows",
        )
        reads = store_selects(ROOT / "src/overseer_disenchant_store.h")
        self.assertEqual({"Pending", "Owned"}, set(reads))
        pending = self.run_sql(reads["Pending"] + ";", "Pending read")
        self.assertEqual(["CrafterA"], [r.split("\t")[1] for r in pending.splitlines()])
        owned = self.run_sql(
            reads["Owned"].replace("?", "'CrafterA'") + ";", "Owned read"
        )
        # The command text rides along so a named row (`guid:<n>`) reaches the
        # drive, and so does the row's age in seconds, a whole number.
        rows = [r.split("\t") for r in owned.splitlines()]
        self.assertEqual([["CrafterA", "craft", "1", "item 2"]], [r[1:5] for r in rows])
        self.assertRegex(rows[0][5], r"^-?\d+$")


def store_selects(path):
    """Map each PrepareStatement name to its SQL, for the SELECT statements."""
    import re

    text = path.read_text()
    out = {}
    for name, body in re.findall(
        r"PrepareStatement\((\w+),\s*((?:\s*(?://[^\n]*\n)?\s*\"(?:[^\"\\]|\\.)*\")+)",
        text,
    ):
        sql = "".join(re.findall(r"\"((?:[^\"\\]|\\.)*)\"", body))
        if sql.lstrip().upper().startswith("SELECT") and " JOIN " in sql:
            out[name] = sql
    return out


if __name__ == "__main__":
    unittest.main()
