-- overseer_level: one row per level change of a family character or a member of
-- a managed guild (Overseer.Natural.Guilds), written when it happens
-- (wow-overseer#533).
--
-- WHY. overseer_event keeps a 'level_up' count per hour for the family only,
-- with the last level reached and nothing about when within the hour. The
-- leveling research could not measure level-ups for the guilds at all, nor line
-- them up against overseer_death. This is the other half of that comparison:
-- one row per change, never coalesced, never updated.
--
-- old_level and new_level both ride on the row because the hook fires on the way
-- down as well as up; the direction is a comparison away rather than a guess.
--
-- A character has at most 79 level changes in its life, so this table needs no
-- per-member gap the way overseer_death does. It is swept on created_at like
-- overseer_death (DEATH_RETENTION_DAYS). Created without `IF NOT EXISTS`, like
-- overseer_death, so re-applying fails loudly. A worldserver that starts before
-- db-import has run this loses rows, not the level change.
CREATE TABLE `overseer_level` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `character_name` VARCHAR(12) NOT NULL,
    `character_guid` INT UNSIGNED NOT NULL DEFAULT 0,
    `old_level` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `new_level` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `map` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `zone` INT UNSIGNED NOT NULL DEFAULT 0,
    `guild_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_character_created` (`character_name`, `created_at`),
    KEY `idx_created` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='One row per level change of a family or managed-guild character (wow-overseer#533)';
