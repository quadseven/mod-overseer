-- The story of a notable item: who looted it, who it went to, and when it went
-- on (mod-overseer#567).
--
-- WHAT THE TABLE COULD NOT SAY. Every rare, epic and legendary item the
-- families' guilds pick up has three moments worth knowing: somebody looted it,
-- somebody may have handed it on, and somebody put it on. Before this change
-- only the third was recorded, only for roster characters, and nothing tied it
-- to the other two. A guild member who looted an epic and passed it to a roster
-- character left exactly one row, and that row could not say where the item
-- came from.
--
-- THREE KINDS, AND KINDS ARE NOT A SCHEMA CHANGE. `kind` is a VARCHAR on
-- purpose (see 2026_08_24_02_overseer_event.sql), so `item_loot` and
-- `item_given` need no DDL. What they need is four facts no existing column
-- holds, and one change to how rows collapse:
--
--   item_guid    the item INSTANCE, `item_instance.guid`. The one value that
--                is the same on the loot row, the hand-over row and the equip
--                row for the same sword, which is what lets a reader join them.
--                0 for every kind that is not about one item instance, which is
--                every row written before this file.
--   counterpart  item_given only: the character the item went TO. The row's
--                own character_name is the giver.
--   via          item_given: give, trade or mail. item_loot: loot, need or
--                greed. How the item moved, because a trade face to face, a
--                letter from a mailbox and a module `give` across a continent
--                are three different stories.
--   source       item_loot: the creature, object or container it came out of.
--                item_given by mail: the mailbox it was posted from. Resolved
--                at the moment it happened, for the reason `level` is: a
--                spawn that despawns cannot be asked afterwards.
--
-- WHY THE UNIQUE KEY GROWS, AND WHY THAT IS SAFE. uq_event is
-- (character_name, kind, subject_id, bucket), and subject_id for these kinds is
-- the item ENTRY. Two copies of the same rare in one hour would collapse onto
-- one row carrying the second copy's guid, and the first copy's story would
-- lose its loot row. Adding item_guid to the key splits rows per instance,
-- which is the grain these kinds are about.
--
-- The CREATE's own warning applies and was thought about: widening a key splits
-- existing rows' futures. Every existing row has item_guid = 0, the default, so
-- every existing row keeps exactly the key it had, and every kind that never
-- sets item_guid collapses exactly as it did. The only rows whose collapsing
-- changes are item_equip rows from now on, which split per instance instead of
-- per entry, and that is correct: two copies of a ring worn on two fingers are
-- two equips. DROP and ADD are one ALTER so no moment exists without the key.
--
-- WHY COLUMNS AND NOT A SECOND TABLE, for the reason 2026_09_09_00 gives: the
-- subject of each row IS the item, `uq_event` already splits by it, and a second
-- table would be a join for one sentence and a second copy of the dedupe key.
--
-- VOLUME. The table's first volume control is a roster gate. These kinds widen
-- it to the guilds roster characters belong to, which is why they carry their
-- own: only quality 3 (rare) and up, and a guild member's equip only for an
-- item this record already saw looted or handed over. The bot factory issues
-- rare sets to random bots on a level-up, and those never reach the table.
--
-- NO NEW INDEX. "Notable items lately" is WHERE kind IN (...) with a time
-- bound, which idx_kind_last_seen covers; item_guid is read off rows that query
-- already found, and a reader joins at most a few hundred of them in memory.
--
-- IDEMPOTENT FROM THE FIRST APPLY, guarded on INFORMATION_SCHEMA exactly as
-- 2026_09_09_00 and 2026_09_05_02 are, because dbimport hashes the whole file
-- and any later edit to this text re-applies it.

SET @item_guid_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_event'
      AND COLUMN_NAME = 'item_guid'
);
SET @add_item_guid = IF(
    @item_guid_missing,
    "ALTER TABLE `overseer_event` ADD COLUMN `item_guid` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'mod-overseer#567: item_loot, item_given, item_equip. The item_instance.guid the row is about, the same on every row about one item so a reader can join them. 0 for every other kind and every row written before #567.'",
    "SELECT 1"
);
PREPARE add_item_guid_stmt FROM @add_item_guid;
EXECUTE add_item_guid_stmt;
DEALLOCATE PREPARE add_item_guid_stmt;

SET @counterpart_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_event'
      AND COLUMN_NAME = 'counterpart'
);
SET @add_counterpart = IF(
    @counterpart_missing,
    "ALTER TABLE `overseer_event` ADD COLUMN `counterpart` VARCHAR(12) NOT NULL DEFAULT '' COMMENT 'mod-overseer#567: item_given only. The character the item went to; character_name is the giver.'",
    "SELECT 1"
);
PREPARE add_counterpart_stmt FROM @add_counterpart;
EXECUTE add_counterpart_stmt;
DEALLOCATE PREPARE add_counterpart_stmt;

SET @via_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_event'
      AND COLUMN_NAME = 'via'
);
SET @add_via = IF(
    @via_missing,
    "ALTER TABLE `overseer_event` ADD COLUMN `via` VARCHAR(16) NOT NULL DEFAULT '' COMMENT 'mod-overseer#567: item_given: give, trade or mail. item_loot: loot, need or greed. Empty for every other kind.'",
    "SELECT 1"
);
PREPARE add_via_stmt FROM @add_via;
EXECUTE add_via_stmt;
DEALLOCATE PREPARE add_via_stmt;

SET @source_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_event'
      AND COLUMN_NAME = 'source'
);
SET @add_source = IF(
    @source_missing,
    "ALTER TABLE `overseer_event` ADD COLUMN `source` VARCHAR(255) NOT NULL DEFAULT '' COMMENT 'mod-overseer#567: item_loot: the creature, object or container the item came out of. item_given by mail: the mailbox it was posted from. Empty where it could not be named, and for every other kind.'",
    "SELECT 1"
);
PREPARE add_source_stmt FROM @add_source;
EXECUTE add_source_stmt;
DEALLOCATE PREPARE add_source_stmt;

-- The key grows only once: if uq_event already names item_guid, this is a
-- no-op on a re-apply.
SET @uq_event_narrow = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_event'
      AND INDEX_NAME = 'uq_event'
      AND COLUMN_NAME = 'item_guid'
);
SET @widen_uq_event = IF(
    @uq_event_narrow,
    "ALTER TABLE `overseer_event` DROP INDEX `uq_event`, ADD UNIQUE KEY `uq_event` (`character_name`, `kind`, `subject_id`, `bucket`, `item_guid`)",
    "SELECT 1"
);
PREPARE widen_uq_event_stmt FROM @widen_uq_event;
EXECUTE widen_uq_event_stmt;
DEALLOCATE PREPARE widen_uq_event_stmt;
