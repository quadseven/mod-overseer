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
-- Accounts named GRUG/UGGA/GROG/BORK/OG can never enter it. Verified live:
-- the server logs ">> 106 random bot accounts", ids 208-313, ours are 314-318.
DROP TABLE IF EXISTS `overseer_roster`;
CREATE TABLE `overseer_roster` (
    `name` VARCHAR(12) NOT NULL,
    -- Set 0 to let a character log out and stay out without deleting the row,
    -- so a character can be parked without losing why it was on the list.
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `note` VARCHAR(255) NOT NULL DEFAULT '',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Characters mod-overseer keeps logged in as masterless bots';
