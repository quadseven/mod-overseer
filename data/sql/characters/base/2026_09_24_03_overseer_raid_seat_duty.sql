-- Each seat's duty in an ordered raid, read off the raider's talent tree.
--
-- WHY THIS COLUMN EXISTS. `role` holds tank, healer or dps, and the site used to
-- pick it by class: a warrior tanked, a priest healed. Read against the realm's
-- own talents on 2026-09-24, that seated Fury and Arms warriors as tanks and
-- Shadow priests as healers in Cave's lineup. The site (wow-overseer's
-- raidroles.py) now reads each raider's talent tree and writes the duty here:
-- `main tank`, `off tank`, `healer`, `melee`, `ranged`, `caster`, or `damage`
-- for a raider whose tree it could not read.
--
-- WHO READS IT. The loot council's master-loot scoring (LoadSeatRoles): a guild
-- raider is scored for the duty rather than a class guess, and the raider in the
-- `main tank` seat is the main tank the council gears first. `role` is kept as it
-- was, so the raid run and an older reader are unchanged.
--
-- ONCE ONLY. The column is added only when it is missing, so a second apply is
-- a no-op, the same guard 2026_09_19_00_overseer_roster_family.sql uses.
SET @duty_column_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_raid_seat'
      AND COLUMN_NAME = 'duty'
);
SET @add_duty_column = IF(
    @duty_column_missing,
    "ALTER TABLE `overseer_raid_seat` ADD COLUMN `duty` VARCHAR(16) NOT NULL DEFAULT '' COMMENT 'main tank, off tank, healer, melee, ranged, caster or damage, from the talent tree' AFTER `role`",
    "SELECT 1"
);
PREPARE add_duty_column_stmt FROM @add_duty_column;
EXECUTE add_duty_column_stmt;
DEALLOCATE PREPARE add_duty_column_stmt;
