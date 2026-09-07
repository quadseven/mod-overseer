-- WHY THE FALL BASELINE GUARD DECLINED, on the death row (mod-overseer#281).
--
-- WHAT THIS ANSWERS THAT NOTHING ELSE CAN. mod-overseer#266 deployed an
-- invariant: a character that is not falling is standing somewhere, and a
-- character that is standing somewhere owes nothing for having got there, so
-- the terrain drive hands the core a fall baseline at that character's own feet
-- once a second. It is deployed and verified present in the running binary, and
-- phantom fall deaths continued anyway.
--
-- The rule is not the problem. `FallBaselineStep` declines in exactly one
-- circumstance, `!mayInspect || falling`; its call site sits ABOVE the
-- recovery's own stand-down and outside the 600 second episode cooldown; and
-- the drive polls once a second. A guard that ran therefore cannot leave
-- `m_lastFallZ` more than one second of movement from the character's feet. The
-- deaths of 2026-09-06 require it to have been 69 or more yards away
-- (0.018 * z_diff - 0.2426 reaches a full health bar at 69.03), and two of those
-- rows measured a descent of exactly zero. Both cannot be true, so the guard was
-- not called.
--
-- A GUARD THAT IS CORRECT AND NEVER CALLED LOOKS IDENTICAL IN THE DATA TO ONE
-- THAT IS CALLED AND WRONG. That is the whole reason for these two columns.
-- Hours went into asking whether the invariant was right before anybody asked
-- whether it was running.
--
-- A MASK AND NOT A FIRST MATCH. Eight inputs can decline the guard and more
-- than one can be true at once. "The flags say falling AND the flags say
-- flying" is itself the diagnosis, so every reason that holds is set rather
-- than whichever the code tested first. The bit order is
-- `TerrainRecoveryMayInspect`'s argument order, and a unit test walks all 256
-- combinations asserting the mask is non-zero exactly when that rule says no.
--
-- WHY THIS IS WORTH A COLUMN RATHER THAN A LOG LINE. The question is "group the
-- deaths by why the guard did not run, and count them", which is a GROUP BY over
-- a number. The log line carries the same answer in words for a reader watching
-- it live, but words cannot be counted and a sentence cannot be indexed.
--
-- SENTINELS ARE NEGATIVE AND MEAN NOT SAMPLED, never zero, and here that
-- distinction is the entire point rather than a convention being observed out of
-- habit. A mask of 0 means the drive LOOKED and nothing declined it, which would
-- refute the diagnosis above and must be able to appear on a row. -1 means the
-- drive has never looked at this character at all. Folding those together is
-- precisely how 223 kill-plane deaths went without an explanation, as
-- 2026_09_05_02_overseer_death_context.sql records at length.
--
-- THE UNDERLYING DISCREPANCY, which these columns are meant to expose and do not
-- themselves fix: this module stands down on movement FLAGS and the core charges
-- fall damage on the absence of AURAS. `Unit::IsFlying()` is
-- `HasMovementFlag(MOVEMENTFLAG_FLYING | MOVEMENTFLAG_DISABLE_GRAVITY)`
-- (Unit.h:1717). `Player::HandleFall` charges unless `HasHoverAura()`,
-- `HasFeatherFallAura()` or `HasFlyAura()` (Player.cpp:14187-14189). A flag set
-- without its aura is a state where the core will charge the character and this
-- module has decided it has no opinion. Two core facts make that state easy to
-- enter and hard to leave: `EffectMovementGenerator::Finalize` opens with
-- `if (!unit->IsCreature()) return;`, so a player's MOVEMENTFLAG_FALLING is
-- never cleared server-side and a spline-driven roster sends no client packets
-- to clear it; and `Player::TeleportTo` reduces the flags to
-- MOVEMENTFLAG_MASK_HAS_PLAYER_STATUS_OPCODE (UnitDefines.h:423), which drops
-- FALLING but KEEPS DISABLE_GRAVITY, CAN_FLY and HOVER, so a stand-down caused
-- by one of those survives every death and every revival.
--
-- IDEMPOTENT FROM THE FIRST APPLY. Same argument as
-- 2026_09_05_02_overseer_death_context.sql: the updater hashes the whole FILE,
-- comments included, so a comment-only edit later reapplies these statements
-- against a database that already has the columns. `ADD COLUMN IF NOT EXISTS` is
-- a parse error on this pipeline's MySQL 8.4, so each column is guarded by the
-- portable INFORMATION_SCHEMA + PREPARE/EXECUTE dance instead.
--
-- NO INDEX. The question is asked over a handful of rows from the last hour,
-- which `idx_driver_created` already reaches, and an index on a column whose
-- whole purpose is to be read once and then removed would outlive its reason.

SET @fall_guard_standdown_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'fall_guard_standdown'
);
SET @add_fall_guard_standdown = IF(
    @fall_guard_standdown_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `fall_guard_standdown` SMALLINT NOT NULL DEFAULT -1 COMMENT 'mod-overseer#281: bitmask of every input that declined the fall baseline guard on the last poll that looked. 1 dead, 2 teleporting, 4 in-flight, 8 flying, 16 falling, 32 in-water, 64 transport, 128 vehicle. 0 means the guard RAN and nothing declined it, which is a reading. -1 means never sampled, which is not.'",
    "SELECT 1"
);
PREPARE add_fall_guard_standdown_stmt FROM @add_fall_guard_standdown;
EXECUTE add_fall_guard_standdown_stmt;
DEALLOCATE PREPARE add_fall_guard_standdown_stmt;

SET @fall_guard_seconds_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'fall_guard_seconds'
);
SET @add_fall_guard_seconds = IF(
    @fall_guard_seconds_missing,
    "ALTER TABLE `overseer_death` ADD COLUMN `fall_guard_seconds` INT NOT NULL DEFAULT -1 COMMENT 'mod-overseer#281: how old the fall_guard_standdown reading is, in seconds. The drive polls every second, so anything above a few means the drive itself was not reaching this character, which is a different fault from standing down. -1 means never sampled.'",
    "SELECT 1"
);
PREPARE add_fall_guard_seconds_stmt FROM @add_fall_guard_seconds;
EXECUTE add_fall_guard_seconds_stmt;
DEALLOCATE PREPARE add_fall_guard_seconds_stmt;
