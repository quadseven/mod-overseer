-- WHAT AN EQUIP DISPLACED: the other half of the comparison, on the row that
-- records the equip (mod-overseer#372).
--
-- WHAT THE TABLE COULD ANSWER BEFORE THIS, AND WHAT IT COULD NOT. Since
-- 2026_08_24_02 a `kind='item_equip'` row has answered who put what on, at what
-- level, where, and how many times in the hour. What it has never answered is
-- what came off. The whole of `detail` for those rows is the literal string
-- `slot <n>`: measured over all 453 rows ever written on the dev realm, the
-- distinct values are `slot 7` (71), `slot 14` (53), `slot 9` (53), `slot 15`
-- (51), `slot 4` (51), `slot 8` (49), `slot 6` (42) and `slot 2` (31), and
-- nothing else.
--
-- An upgrade is a comparison, and the comparison was never stored. So the
-- armory can render a list of equips and can say nothing about progression: not
-- what improved, not by how much, not which drop mattered. Worse, it cannot
-- settle an ambiguity that was measured today. A roster character's row at
-- 15:02 records one weapon going into slot 15 and the character's live
-- inventory afterwards holds a different one. Either it was swapped straight
-- back seconds later, or the equip never stuck, and the row reads identically
-- under both. That is the same failure this table was created to abolish: the
-- server held the fact and threw it away.
--
-- WHY COLUMNS AND NOT A SECOND TABLE. `overseer_death` got a table of its own
-- because a death has no subject and must not be coalesced, so the event row
-- was the wrong grain for it. An equip is not in that position: its subject IS
-- the item, `uq_event` already splits one hour's equips per item, and what is
-- being added is one more fact about the subject the row already names. A
-- second table would mean a join for a single sentence and a second copy of the
-- de-duplication key. So this follows the migration path the CREATE in
-- 2026_08_24_02 lays out for exactly this case: a new dated file with explicit
-- ALTERs, never an edit to that file.
--
-- WHY BOTH SIDES CARRY QUALITY AND ITEM LEVEL, denormalised, rather than a
-- reader joining `item_template` for them. Two reasons, and the second is the
-- one that decided it. First, "was this an upgrade" is the question the armory
-- exists to answer and a second round trip per row to answer it is the cost
-- #372 was filed to remove. Second, `item_template` is content: a patch that
-- rebalances an item changes what a join returns, so a judgement rendered from
-- a join is a judgement about today's template applied to last month's equip. A
-- reading taken at the moment of the event cannot be retro-dated. This is the
-- same argument `level` on this table already makes against reading a level
-- back out of `characters`.
--
-- AND WHY NO STORED VERDICT. There is deliberately no `is_upgrade` column and
-- no `item_level_delta`. A delta is arithmetic over two columns that are both
-- here, and storing it adds a third value that can disagree with them. A
-- verdict is worse: "upgrade" is a policy - this module's own scorer weighs
-- stats, class, armour proficiency and rolled properties, and that policy has
-- already changed twice - and a verdict computed under an old policy cannot be
-- recomputed from the row, while the inputs can always be re-judged under a new
-- one. Store what was true; derive what is thought.
--
-- `prior_state` IS THE COLUMN THAT MATTERS AND IT HAS FOUR VALUES.
--
--   'item'     the slot held `prior_id` and this equip displaced it
--   'empty'    the module looked, and the slot was bare
--   'unknown'  the module had never observed this slot for this character
--   ''         the row predates this migration
--
-- The third and fourth exist because folding either into 'empty' would be a
-- lie that reads like a finding: "the character's first ever shoulder piece"
-- written over an equip that replaced something nobody had looked at. Unknown
-- is never a plausible value - `overseer_death`'s sentinels make the same
-- argument at length, and 223 unexplained deaths are what it cost to learn.
-- The default is '' rather than 'unknown' precisely so the two stay apart:
-- every row already in the table gets '' and none of them is claiming to have
-- observed anything.
--
-- A VARCHAR AND NOT AN ENUM, for the reason the CREATE gives about `kind`: this
-- project has twice shipped C++ that compiled, INSERTed, and silently wrote ''
-- because an enum did not carry the new value yet.
--
-- WHAT DOES NOT CHANGE. `detail` keeps its existing `slot <n>` opening and
-- gains a suffix naming what came out, so a reader parsing the leading slot
-- number is unaffected. No existing column changes type, meaning or default; no
-- row is rewritten; `uq_event` is untouched, so nothing about how repeats
-- collapse is different. Every column here is additive with a default, which is
-- what makes the old shape still valid.
--
-- NO NEW INDEX. "Which drops mattered this week" is `WHERE kind='item_equip'`
-- with a time bound, which `idx_kind_last_seen` already covers; the new columns
-- are read off rows that query has already found. An index nothing filters on
-- is write cost with no reader, which is the discipline
-- 2026_09_05_02_overseer_death_context.sql settled on.
--
-- IDEMPOTENT FROM THE FIRST APPLY. AzerothCore's dbimport hashes the whole
-- file, comments included, so any later edit to this text re-applies it against
-- a database that already has these columns. Each ALTER is therefore guarded on
-- INFORMATION_SCHEMA rather than written bare, and a re-apply is a no-op rather
-- than an error. This is the pattern 2026_09_05_02 established; it is not
-- `IF NOT EXISTS`, which MySQL does not offer for ADD COLUMN.

SET @subject_quality_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_event'
      AND COLUMN_NAME = 'subject_quality'
);
SET @add_subject_quality = IF(
    @subject_quality_missing,
    "ALTER TABLE `overseer_event` ADD COLUMN `subject_quality` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'mod-overseer#372: item_equip only. ItemTemplate::Quality of the item that went ON, read at the moment of the equip so a later content patch cannot retro-date a judgement. 0 poor, 1 common, 2 uncommon, 3 rare, 4 epic. 0 also means not applicable for every other kind, which is safe because no other kind writes it.'",
    "SELECT 1"
);
PREPARE add_subject_quality_stmt FROM @add_subject_quality;
EXECUTE add_subject_quality_stmt;
DEALLOCATE PREPARE add_subject_quality_stmt;

SET @subject_item_level_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_event'
      AND COLUMN_NAME = 'subject_item_level'
);
SET @add_subject_item_level = IF(
    @subject_item_level_missing,
    "ALTER TABLE `overseer_event` ADD COLUMN `subject_item_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'mod-overseer#372: item_equip only. ItemTemplate::ItemLevel of the item that went ON, read at the moment of the equip. Paired with prior_item_level this is the whole of what an upgrade needs, with no join.'",
    "SELECT 1"
);
PREPARE add_subject_item_level_stmt FROM @add_subject_item_level;
EXECUTE add_subject_item_level_stmt;
DEALLOCATE PREPARE add_subject_item_level_stmt;

SET @prior_state_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_event'
      AND COLUMN_NAME = 'prior_state'
);
SET @add_prior_state = IF(
    @prior_state_missing,
    "ALTER TABLE `overseer_event` ADD COLUMN `prior_state` VARCHAR(16) NOT NULL DEFAULT '' COMMENT 'mod-overseer#372: item_equip only. What the slot held immediately before this equip. item = it held prior_id and this displaced it. empty = the module looked and the slot was bare. unknown = the module had never observed this slot for this character, which is NOT the same as bare and must never be read as it. Empty string means the row predates #372.'",
    "SELECT 1"
);
PREPARE add_prior_state_stmt FROM @add_prior_state;
EXECUTE add_prior_state_stmt;
DEALLOCATE PREPARE add_prior_state_stmt;

SET @prior_id_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_event'
      AND COLUMN_NAME = 'prior_id'
);
SET @add_prior_id = IF(
    @prior_id_missing,
    "ALTER TABLE `overseer_event` ADD COLUMN `prior_id` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'mod-overseer#372: item_equip only. The item entry this equip displaced. Meaningful only where prior_state = item; 0 everywhere else, and 0 on its own is not evidence of an empty slot - read prior_state for that.'",
    "SELECT 1"
);
PREPARE add_prior_id_stmt FROM @add_prior_id;
EXECUTE add_prior_id_stmt;
DEALLOCATE PREPARE add_prior_id_stmt;

SET @prior_name_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_event'
      AND COLUMN_NAME = 'prior_name'
);
SET @add_prior_name = IF(
    @prior_name_missing,
    "ALTER TABLE `overseer_event` ADD COLUMN `prior_name` VARCHAR(255) NOT NULL DEFAULT '' COMMENT 'mod-overseer#372: item_equip only. The displaced item name as it read at the time. The human copy of prior_id, and may be empty where the template could not be resolved - exactly as subject_name may be.'",
    "SELECT 1"
);
PREPARE add_prior_name_stmt FROM @add_prior_name;
EXECUTE add_prior_name_stmt;
DEALLOCATE PREPARE add_prior_name_stmt;

SET @prior_quality_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_event'
      AND COLUMN_NAME = 'prior_quality'
);
SET @add_prior_quality = IF(
    @prior_quality_missing,
    "ALTER TABLE `overseer_event` ADD COLUMN `prior_quality` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'mod-overseer#372: item_equip only. ItemTemplate::Quality of the displaced item, read at the moment of the equip. subject_quality > prior_quality with prior_state = item is a quality upgrade, answered from one row.'",
    "SELECT 1"
);
PREPARE add_prior_quality_stmt FROM @add_prior_quality;
EXECUTE add_prior_quality_stmt;
DEALLOCATE PREPARE add_prior_quality_stmt;

SET @prior_item_level_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_event'
      AND COLUMN_NAME = 'prior_item_level'
);
SET @add_prior_item_level = IF(
    @prior_item_level_missing,
    "ALTER TABLE `overseer_event` ADD COLUMN `prior_item_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'mod-overseer#372: item_equip only. ItemTemplate::ItemLevel of the displaced item, read at the moment of the equip. The delta against subject_item_level is deliberately NOT stored: it is arithmetic over two columns that are both here, and a third copy can only disagree with them.'",
    "SELECT 1"
);
PREPARE add_prior_item_level_stmt FROM @add_prior_item_level;
EXECUTE add_prior_item_level_stmt;
DEALLOCATE PREPARE add_prior_item_level_stmt;
