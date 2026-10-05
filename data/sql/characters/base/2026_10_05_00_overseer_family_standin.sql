-- overseer_family_standin: at most one row per family, naming a family member
-- who sits out of the family's dungeon campaign and the guild member who
-- stands in for it (operator request, 2026-10-05: when a family member is busy
-- tailoring, put a different damage dealer, tank or healer in).
--
-- ONE WRITER, ONE READER. The bridge inserts a row when it sends a member off
-- to craft and deletes it when the member is done. The module only reads it,
-- once per dungeon poll, and never writes it.
--
-- `family` is the family HEAD's name (the roster's `lead` member), not the
-- roster's `family` column. The UNIQUE key is what makes "one stand-in per
-- family" true in the table; the module also refuses a guest asked for by two
-- families, a guest who is on the roster, and a row that would sit the head
-- out (OverseerDecisions::ChooseStandins).
--
-- `seat` is the role the guest answers the dungeon finder's role check with.
--
-- THE MODULE APPLIES A ROW ONLY BETWEEN RUNS. It is read while the family's
-- dungeon coordinator is IDLE or RESETTING and frozen for the rest of the run,
-- so a row deleted mid-run keeps the guest in until that run ends
-- (OverseerDecisions::NextFrozenStandin).
CREATE TABLE IF NOT EXISTS `overseer_family_standin` (
    `id` INT NOT NULL AUTO_INCREMENT,
    `family` VARCHAR(12) NOT NULL,
    `out_name` VARCHAR(12) NOT NULL,
    `in_name` VARCHAR(12) NOT NULL,
    `seat` ENUM('tank','healer','dps') NOT NULL,
    `reason` VARCHAR(160) NOT NULL DEFAULT '',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uniq_family` (`family`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='One family member sitting out and the guild member standing in for it';
