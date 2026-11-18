-- New roster rows without an explicit cohort use a neutral value.
-- This changes only the column default: existing family values are untouched.
SET @family_default_needs_neutral = (
    SELECT COUNT(*) > 0 AND COALESCE(MAX(COLUMN_DEFAULT = ''), 0) = 0
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_roster'
      AND COLUMN_NAME = 'family'
);
SET @neutralize_roster_family_default = IF(
    @family_default_needs_neutral,
    "ALTER TABLE `overseer_roster` ALTER COLUMN `family` SET DEFAULT ''",
    "SELECT 1"
);
PREPARE neutralize_roster_family_default_stmt
    FROM @neutralize_roster_family_default;
EXECUTE neutralize_roster_family_default_stmt;
DEALLOCATE PREPARE neutralize_roster_family_default_stmt;
