-- The travel book's refusal memory, kept across a worldserver roll
-- (wow-overseer#1193, layer 5 of wow-overseer#789).
--
-- WHY THIS TABLE EXISTS. The travel drive refuses an errand that was killing a
-- character, or that was going nowhere, for a cool-off, and counts the lethal
-- releases of one target in a row so a second release doubles it (#776). That
-- memory lived in the process only, so every roll forgot it: an aim still in
-- overseer_roster.travel_npc, or one the bridge wrote again, was walked from
-- zero strikes, straight back to what had been killing the character.
--
-- ONE ROW PER CHARACTER, the book's own shape: its one refusal and its one
-- strike record, each overwritten by the next. The module writes the row when
-- it refuses or strikes, and reads back at start-up only what is still inside
-- its cool-off (OverseerDecisions::RestoreTravelRefusal). Times are Unix
-- seconds. The table never grows past the roster.
--
-- NOTHING HERE IS REQUIRED. Without the table the module keeps the memory in
-- the process only, as before, and says so once at start-up.
CREATE TABLE IF NOT EXISTS `overseer_travel_refusal` (
    `name` VARCHAR(12) NOT NULL,
    `refused_target` VARCHAR(255) NOT NULL DEFAULT '',
    `reason` VARCHAR(255) NOT NULL DEFAULT '',
    `refused_at` BIGINT NOT NULL DEFAULT 0,
    `strike_target` VARCHAR(255) NOT NULL DEFAULT '',
    `strikes` INT UNSIGNED NOT NULL DEFAULT 0,
    `struck_at` BIGINT NOT NULL DEFAULT 0,
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Travel errands refused per character, kept across a worldserver restart';
