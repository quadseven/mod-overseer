-- Ask a living character what it is actually doing (infra#2759).
--
-- WHY. Every check on this family so far has gone through acore_characters,
-- and that table is up to fifteen minutes stale: PlayerSaveInterval is 900000
-- and each player's save timer is staggered from its own login. After shipping
-- the training pass in #2756 the database said a level 9 paladin still had zero
-- spells for a quarter of an hour AFTER he had been taught them. The change had
-- worked and every available instrument said it had not.
--
-- The reverse is worse and has already happened repeatedly: a command reports
-- `delivered` because the row was handed over, while the thing it asked for
-- never occurred. `talents spec prot pve` returned delivered and changed
-- nothing. There was no way to ask the character itself.
--
-- WHY NOT kind='gm'. `.pinfo`, `.gps` and friends would answer some of this,
-- but that path runs through the character's own session and a playerbot
-- session carries no GM security - with account 318 at gmlevel 3 both are
-- refused. It only works while a real client holds the character, which is
-- exactly when it is least needed.
--
-- WHY READ-ONLY. A probe answers from the live Player* and mutates nothing, so
-- it cannot be the reason an experiment succeeded. Anything that changes the
-- world still goes through kind='bot' or kind='chat', where it is visible as a
-- command rather than hidden inside an inspection.
-- RESTATED TO THE FINAL UNION AFTER THE FACT (#1343). dbimport re-applies
-- any file whose hash changed, and this statement's shorter list, re-applied
-- after later files had widened the column, shrank the ENUM under live rows
-- (an error in strict mode, or truncation). It now names the final union
-- the chain ends at, so a re-apply in any order cannot shrink it; a first
-- application still ends at the same schema, because every file in the
-- chain now restates this same list.
ALTER TABLE `overseer_command`
    MODIFY COLUMN `kind` ENUM('bot','chat','gm','probe','give','share','trade','job','sell','bank','auction','mail','repair','buy','bind','hearth','summon','conjure','cast','guild','quest','naturalize','disenchant') NOT NULL DEFAULT 'bot';

-- The add is guarded the way 2026_08_26_01_overseer_roster_job.sql guards
-- its own (#1343): an unguarded ADD COLUMN aborts a re-apply of this file
-- with a duplicate-column error.
-- MEDIUMTEXT because `detail` is VARCHAR(255) and a spell list alone
-- exceeds that at level 11. detail keeps its job - short status and error
-- strings - and this carries the payload, so nothing that reads detail
-- today has to learn about probes.
SET @result_column_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_command'
      AND COLUMN_NAME = 'result'
);
SET @add_result_column = IF(
    @result_column_missing,
    "ALTER TABLE `overseer_command` ADD COLUMN `result` MEDIUMTEXT NULL DEFAULT NULL AFTER `detail`",
    "SELECT 1"
);
PREPARE add_result_column_stmt FROM @add_result_column;
EXECUTE add_result_column_stmt;
DEALLOCATE PREPARE add_result_column_stmt;
