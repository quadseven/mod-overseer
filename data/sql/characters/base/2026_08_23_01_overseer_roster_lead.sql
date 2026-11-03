-- Which roster character leads the party (infra#2724).
--
-- WHY THE MODULE NEEDS TO KNOW. mod-overseer forms the party from whoever is
-- online in name order, so leadership landed on whoever sorted first - Bork,
-- the youngest, in charge of his own father. The bridge knows the family
-- relationships and the module does not, so the bridge writes the answer here.
--
-- WHY NOT A GM COMMAND. `.group leader <name>` works, but only from a session
-- that carries GM security, and a playerbot session does not: with account 318
-- at gmlevel 3, `.pinfo` and `.gps` issued as the bot are both refused. The
-- whole kind='gm' path only works while a real client holds the character, so
-- correcting leadership from the bridge produced an error every cycle forever.
-- Guarded the way 2026_08_26_01_overseer_roster_job.sql guards its own
-- (#1343): dbimport re-applies a hash-changed file, and an unguarded
-- ADD COLUMN aborts the second run with a duplicate-column error.
SET @lead_column_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_roster'
      AND COLUMN_NAME = 'lead'
);
SET @add_lead_column = IF(
    @lead_column_missing,
    "ALTER TABLE `overseer_roster` ADD COLUMN `lead` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'This character should lead the roster party'",
    "SELECT 1"
);
PREPARE add_lead_column_stmt FROM @add_lead_column;
EXECUTE add_lead_column_stmt;
DEALLOCATE PREPARE add_lead_column_stmt;
