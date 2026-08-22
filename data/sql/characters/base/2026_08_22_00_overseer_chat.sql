-- mod-overseer, chat bridge (infra#2597).
--
-- Adds the two halves of a synced conversation:
--   overseer_chat        world -> Discord. One row per line a WATCHED
--                        character could actually hear, so the relay is the
--                        character's own chat log rather than a firehose of
--                        every line 500 bots say across the world.
--   overseer_chat_watch  who is being listened to, and on which channels.
--
-- and widens overseer_command so the same queue can carry three kinds of
-- instruction instead of only bot commands. One table, one poller, one
-- status contract - see mod_overseer.cpp.
--
-- Applied once by dbimport, which records it in acore_characters.updates.

CREATE TABLE IF NOT EXISTS `overseer_chat` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  -- The watched character this line reached. One spoken line can produce
  -- several rows when several watched characters are in earshot.
  `heard_by` VARCHAR(12) NOT NULL,
  `sender_name` VARCHAR(12) NOT NULL,
  `sender_guid` INT UNSIGNED NOT NULL DEFAULT 0,
  `sender_is_bot` TINYINT(1) NOT NULL DEFAULT 0,
  `channel` VARCHAR(16) NOT NULL
    COMMENT 'say|yell|emote|whisper|party|raid|guild|officer|channel',
  `channel_name` VARCHAR(64) NOT NULL DEFAULT ''
    COMMENT 'public channel name, empty for every other kind',
  `text` VARCHAR(512) NOT NULL,
  -- Set only AFTER the line reaches Discord, so a bridge restart re-sends
  -- rather than silently dropping the line.
  `relayed` TINYINT(1) NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_relayed` (`relayed`, `id`),
  KEY `idx_heard` (`heard_by`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `overseer_chat_watch` (
  `name` VARCHAR(12) NOT NULL,
  -- Comma-separated channel kinds. Public channels (Trade, General,
  -- LookingForGroup) are NOT supported at all - not merely off by default.
  -- Channel::IsOn is private in the core and there is no public way to ask
  -- whether a player is in a given channel, so membership cannot be tested;
  -- recording without it would relay lines the watcher is not in. Asking for
  -- `channel` here is rejected rather than silently ignored.
  `channels` VARCHAR(255) NOT NULL
    DEFAULT 'say,yell,emote,whisper,party,raid,guild,officer',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT IGNORE INTO `overseer_chat_watch` (`name`) VALUES ('Grug');

-- 'claimed' is the in-flight state. mod-overseer moves a row into it
-- synchronously BEFORE carrying the command out, so a crash mid-command
-- leaves it claimed and it is never run twice. A row still sitting in
-- 'claimed' means the worldserver died while doing it.
ALTER TABLE `overseer_command`
  MODIFY COLUMN `status` ENUM('pending','claimed','delivered','error')
    NOT NULL DEFAULT 'pending',
  ADD COLUMN `kind` ENUM('bot','chat','gm') NOT NULL DEFAULT 'bot' AFTER `command`,
  ADD COLUMN `channel` VARCHAR(16) NOT NULL DEFAULT '' AFTER `kind`,
  ADD COLUMN `target_arg` VARCHAR(12) NOT NULL DEFAULT '' AFTER `channel`,
  -- Which worldserver run holds the claim. The claim is only honoured by the
  -- run whose token is read back, so a conditional UPDATE that another process
  -- won cannot be mistaken for one this process won.
  ADD COLUMN `claimed_by` VARCHAR(36) NOT NULL DEFAULT '' AFTER `status`;
