-- WHAT THE GHOST DID ABOUT ITS CORPSE, on the death row (mod-overseer#664).
--
-- A released character is asked what a player would do: run back to the
-- corpse, take the spirit healer at the graveyard, or wait clear of the corpse
-- until it is safe (OverseerDecisions::DecideGhostRecovery). The answer is
-- written here on the character's newest death row, and rewritten when it
-- changes, so the row carries the last choice made for that death:
--
--   ''              not decided: the character never became a ghost on its
--                   corpse's map in the open world (a dungeon run, an
--                   instance, a healer's rez, a release on another map), or
--                   the row predates this column
--   'corpse_run'    the dead engine walked back and reclaimed the corpse
--   'wait'          held clear of the corpse while a threat stood near it
--   'spirit_healer' took the spirit healer's resurrection at the graveyard
--   'ladder'        kept off the corpse and handed to the stuck-revival
--                   ladder, because the spirit healer's graveyard was unsafe
--
-- WHY A COLUMN AND NOT recovery_rung. recovery_rung, recovery_prev_rung and
-- recovery_seconds are the movement recovery ladder's last remedy BEFORE the
-- death, read at the moment of death, and the death attribution reads them as
-- that. This is a decision made AFTER the death, so it gets its own column
-- rather than a second meaning in one of theirs.
--
-- IDEMPOTENT FROM THE FIRST APPLY. The updater hashes the whole file, comments
-- included, so a comment edit reapplies this. `ADD COLUMN IF NOT EXISTS` is a
-- parse error on MySQL 8.4, so the column is guarded by INFORMATION_SCHEMA and
-- PREPARE/EXECUTE, as 2026_09_06_00_overseer_death_fall_guard.sql does.

SET @ghost_recovery_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'ghost_recovery'
);
SET @add_ghost_recovery = IF(
    @ghost_recovery_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `ghost_recovery` VARCHAR(16) NOT NULL DEFAULT '' COMMENT 'mod-overseer#664: what the ghost did about its corpse, last choice made: corpse_run, wait, spirit_healer or ladder. Empty means not decided for this death.'",
    "SELECT 1"
);
PREPARE add_ghost_recovery_stmt FROM @add_ghost_recovery;
EXECUTE add_ghost_recovery_stmt;
DEALLOCATE PREPARE add_ghost_recovery_stmt;
