-- overseer_realm_hour: the whole realm's deaths and level-ups, one counter row
-- per hour, level band, zone, killer entry and cohort (mod-overseer#1552).
--
-- WHY. overseer_death and overseer_level cover the family and the managed guilds
-- only, so the dying of every other bot was measured nowhere and "is this rate
-- normal for the realm" had no answer. This table counts every character the
-- module sees die or level without keeping a row per character: the table is
-- sized by band, zone and killer, never by who died.
--
-- level_band is the first level of a ten-level band (1, 11, 21 ...). killer_entry
-- is 0 for a level-up and for a death with no creature killer. managed is 1 for
-- the family and the managed guilds, 0 for everyone else, so the managed share
-- reads off the same rows. Rows of one hour are added to, not replaced, so a
-- worldserver restart inside an hour does not lose the first part of it.
--
-- Swept on hour_start like overseer_death (DEATH_RETENTION_DAYS). A worldserver
-- that starts before db-import has run this loses counts, not the death.
CREATE TABLE IF NOT EXISTS `overseer_realm_hour` (
    `hour_start` DATETIME NOT NULL,
    `level_band` TINYINT UNSIGNED NOT NULL,
    `zone` INT UNSIGNED NOT NULL DEFAULT 0,
    `killer_entry` INT UNSIGNED NOT NULL DEFAULT 0,
    `managed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `deaths` INT UNSIGNED NOT NULL DEFAULT 0,
    `level_ups` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`hour_start`, `level_band`, `zone`, `killer_entry`, `managed`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Realm-wide deaths and level-ups per hour, band, zone, killer and cohort (mod-overseer#1552)';
