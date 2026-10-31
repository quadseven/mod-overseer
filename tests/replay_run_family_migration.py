"""Apply the real ledger migrations twice against an isolated MySQL 8 database."""

import json
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SQL = ROOT / "data/sql/characters/base"
DATABASE_SQL = (
    "CREATE DATABASE IF NOT EXISTS overseer_run_family_replay "
    "CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;\n"
    "USE overseer_run_family_replay;\n"
)


class RunFamilyMigrationTest(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
