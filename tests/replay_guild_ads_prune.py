"""The guild-advertisement prune reaches the playerbots database (#2252 in the
site's tracker, originally #1640 there).

The prune of the four guild-search rows of ai_playerbot_texts was merged as an
SQL file in the site's repository, where nothing applies it: the realm build
compiles the pinned playerbots tree plus this repository's patches, and the
playerbots module runs its own database updater over its own
data/sql/playerbots/updates/ folder (src/Script/Playerbots.cpp at the pin,
`ModuleDBUpdater::Update` under `Playerbots.Updates.EnableDatabases`, default
on). So the prune is carried here as a playerbots patch that adds the update
file to that folder.

This applies the patch to an empty tree, checks the file lands in the folder
the updater reads and sorts after the pin's last update, and runs its SQL
against a copy of the seed rows (the four targets plus their neighbors and a
row of another class, texts as the pin seeds them): exactly the four rows go.

No realm, database server or network is used.
"""

import sqlite3
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE.parent / "patches/mod-playerbots/0025-the-guild-advertisements-are-pruned-from-bot-speech.patch"
UPDATES = Path("data/sql/playerbots/updates")
# The last update file in that folder at the pin the patches apply to.
PIN_LAST_UPDATE = "2026_10_05_01_ai_playerbot_rpg_gather_texts.sql"

TARGETS = {741, 743, 801, 816}
SEED_ROWS = [
    (739, "suggest_something", "Wanna party in %zone_name."),
    (740, "suggest_something", "Anyone is looking for %my_role?"),
    (741, "suggest_something", "%my_role is looking for guild."),
    (742, "suggest_something", "Looking for gold."),
    (743, "suggest_something", "%my_role wants to join a good guild."),
    (744, "suggest_something", "Need a friend."),
    (801, "suggest_something", "any guilds willing to carry %my_role?"),
    (816, "suggest_something", "any leveling guilds out there?"),
    (873, "suggest_something_toxic",
     "why is my keyboard not working, is it because I don\u2019t have enough friends"),
]


def applied_tree(tmp: Path) -> Path:
    subprocess.run(["git", "init", "-q", str(tmp)], check=True)
    subprocess.run(["git", "-C", str(tmp), "apply", "--whitespace=error", str(PATCH)],
                   check=True)
    return tmp


class GuildAdsPrune(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tree = applied_tree(Path(self._tmp.name))
        added = sorted((self.tree / UPDATES).glob("*.sql"))
        self.assertEqual(len(added), 1, "the patch adds exactly one update file")
        self.update = added[0]

    def tearDown(self):
        self._tmp.cleanup()

    def test_the_update_lands_where_the_playerbots_updater_reads(self):
        self.assertEqual(self.update.parent, self.tree / UPDATES)
        self.assertGreater(self.update.name, PIN_LAST_UPDATE,
                           "the update sorts after the pin's last one, so it runs last")

    def test_it_deletes_exactly_the_four_guild_searches(self):
        db = sqlite3.connect(":memory:")
        db.execute("CREATE TABLE `ai_playerbot_texts` (`id` INTEGER PRIMARY KEY,"
                   " `name` TEXT NOT NULL, `text` TEXT NOT NULL)")
        db.executemany("INSERT INTO `ai_playerbot_texts` VALUES (?, ?, ?)", SEED_ROWS)
        db.executescript(self.update.read_text())
        left = {row[0] for row in db.execute("SELECT `id` FROM `ai_playerbot_texts`")}
        self.assertEqual(left, {row[0] for row in SEED_ROWS} - TARGETS)

    def test_a_target_id_in_another_class_is_left_alone(self):
        db = sqlite3.connect(":memory:")
        db.execute("CREATE TABLE `ai_playerbot_texts` (`id` INTEGER PRIMARY KEY,"
                   " `name` TEXT NOT NULL, `text` TEXT NOT NULL)")
        db.execute("INSERT INTO `ai_playerbot_texts` VALUES (741, 'other_class', 'x')")
        db.executescript(self.update.read_text())
        self.assertEqual(db.execute("SELECT COUNT(*) FROM `ai_playerbot_texts`").fetchone()[0], 1,
                         "each delete is guarded to the suggest_something class")


if __name__ == "__main__":
    unittest.main()
