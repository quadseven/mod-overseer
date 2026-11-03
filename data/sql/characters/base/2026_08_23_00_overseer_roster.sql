-- The characters the overseer keeps logged in and playing with nobody at the
-- keyboard (infra#2656).
--
-- WHY A TABLE AND NOT A CONFIG. The set changes as characters are created and
-- retired, and a conf change costs a worldserver restart - which takes the
-- world down for everyone. The bridge owns the rows; the module reads them.
--
-- WHY NOT playerbots_account_type. That is the documented route and it does
-- not work for named characters: RandomPlayerbotMgr only ever drives accounts
-- whose usernames match AiPlayerbot.RandomBotAccountPrefix, because
-- IsRandomBot() gates on IsInRandomAccountList(), and that list is built at
-- startup by walking "<prefix>0".."<prefix>N" (RandomPlayerbotFactory.cpp).
-- Accounts with ordinary names can never enter it. Verified live: the
-- server logs how many random bot accounts it generated at startup, and
-- the overseer's named accounts sit outside that generated range.
--
-- NO DROP, AND IF NOT EXISTS (#1343). This file once dropped and recreated
-- the table, which was safe only while it could never run twice. dbimport
-- re-applies any file whose hash changed, so the first later edit to this
-- file (a comment scrub, as it happened) would have deleted every roster
-- row and every column later migrations added. The create below is a
-- no-op on any database that already has the table, which is what makes
-- the edit that carries this fix safe to land: the re-apply it triggers
-- is this harmless version.
CREATE TABLE IF NOT EXISTS `overseer_roster` (
    `name` VARCHAR(12) NOT NULL,
    -- Set 0 to let a character log out and stay out without deleting the row,
    -- so a character can be parked without losing why it was on the list.
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `note` VARCHAR(255) NOT NULL DEFAULT '',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Characters mod-overseer keeps logged in as masterless bots';
