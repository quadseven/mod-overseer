-- THE FALL GUARD STAND-DOWN COLUMN MEANS SOMETHING SLIGHTLY DIFFERENT NOW
-- (mod-overseer#291), so it says so. Comment only: no column is added, no data
-- is touched, no index changes.
--
-- WHAT THE COLUMN ANSWERED, AND WHAT IT ANSWERED WITH. 2026_09_06_00 added
-- `fall_guard_standdown` to find out why the fall baseline guard was declining
-- to keep the core's baseline under a character's feet. It answered on the
-- first sample after it deployed: the mask was 16, FALL_GUARD_FALLING, on every
-- phantom fall death, with `fall_guard_seconds` of 0 or 1. The guard was
-- reached every poll and declined every poll, on characters at full health
-- whose measured descent was 1.63 and 1.88 yards. The flag said falling while
-- the character stood still.
--
-- AND IT WAS RE-SET RATHER THAN NEVER CLEARED, which is the finding that
-- decided the fix. `Player::TeleportTo` reduces the movement flags to
-- MOVEMENTFLAG_MASK_HAS_PLAYER_STATUS_OPCODE (UnitDefines.h:423), which drops
-- FALLING, so every graveyard revival clears it. The below-terrain recovery,
-- which cannot run at all while `Unit::IsFalling` is true, was observed running
-- twice between two masked deaths at 01:41:44 and 01:41:53. Clear then, and set
-- again by the next death. A change that cleared the flag once would have fixed
-- nothing.
--
-- SO THE GUARD STOPPED ASKING AND STARTED MEASURING. It no longer consults
-- `Unit::IsFalling` or `Unit::IsFlying` at all. It declines on the core's own
-- charging gate, `HasHoverAura() || HasFeatherFallAura() || HasFlyAura()`,
-- which is exactly what `Player::HandleFall` consults (Player.cpp:14187-14189),
-- and on a fall it measures for itself from two positions one second apart.
--
-- WHAT THAT DOES TO THIS COLUMN. Bits 0 to 7 keep the exact meanings they were
-- deployed with, deliberately, so rows either side of the change can still be
-- compared and counted together. What changed is which of them DECIDE
-- anything:
--
--   bits 0, 1, 2, 5, 6, 7   still decline the guard
--   bits 3 and 4            flying and falling, still RECORDED, no longer decline
--   bit 8  (256)            an aura the core itself checks: the fall is free
--   bit 9  (512)            measured to be losing height fast enough to be a fall
--
-- So "did the guard run" is now "is any bit other than 3 and 4 clear of the
-- mask", and the pair of bits 3 and 4 became the evidence rather than the
-- cause. A row with 16 alone, which was the whole of the problem, now means the
-- flag was stuck and the guard ran anyway.
--
-- -1 STILL MEANS NOT SAMPLED and 0 still means the drive looked and nothing
-- declined it. Those two were kept apart when the column was created and are
-- kept apart here, for the reason 2026_09_05_02_overseer_death_context.sql
-- gives at length.
--
-- IDEMPOTENT FROM THE FIRST APPLY. The updater hashes the whole file, comments
-- included, so a later comment edit reapplies this. `MODIFY COLUMN` restates
-- the type and default exactly as 2026_09_06_00 created them, so reapplying is
-- a no-op rather than a migration, and it is guarded on the column existing at
-- all so that a database which has not run 2026_09_06_00 is not an error here.

SET @fall_guard_standdown_present = (
    SELECT COUNT(*) = 1 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_death'
      AND COLUMN_NAME = 'fall_guard_standdown'
);
SET @recomment_fall_guard_standdown = IF(
    @fall_guard_standdown_present,
    "ALTER TABLE `overseer_death` MODIFY COLUMN `fall_guard_standdown` SMALLINT NOT NULL DEFAULT -1 COMMENT 'mod-overseer#281/#291: bitmask of what stood the fall baseline guard down on the last poll that looked. 1 dead, 2 teleporting, 4 in-flight, 8 flying, 16 falling, 32 in-water, 64 transport, 128 vehicle, 256 an aura the core itself checks, 512 measured to be falling. Since #291, values 8 (flying) and 16 (falling) are RECORDED but no longer decline the guard: both are movement flags and both were observed stuck on while a character stood still at full health. The guard now declines on 1, 2, 4, 32, 64, 128, 256 and 512 only. 0 means it ran; -1 means never sampled, which is not the same thing.'",
    "SELECT 1"
);
PREPARE recomment_fall_guard_standdown_stmt FROM @recomment_fall_guard_standdown;
EXECUTE recomment_fall_guard_standdown_stmt;
DEALLOCATE PREPARE recomment_fall_guard_standdown_stmt;
