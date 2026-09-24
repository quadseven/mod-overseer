-- The loot council: one row per drop a family's group has to hand out.
--
-- WHY THIS TABLE EXISTS. A family's party runs need before greed and a raid
-- runs master loot (OverseerDecisions::LootRulesFor). For a weapon or a piece
-- of armour at uncommon or better, the module writes the drop here with every
-- candidate's upgrade already scored, the site's loot_council judgment names
-- the recipient, and the module carries it out: the recipient needs on the
-- roll while everybody else passes, or the master looter hands it over. A
-- drop the site does not answer inside its wait is decided by the module's own
-- heuristic (LootCouncilHeuristic), so a silent site never loses a drop.
--
-- `council_key` is "roll:<roll item guid>" for a group-loot roll and
-- "ml:<creature raw guid>:<loot slot>" for a master-loot drop.
-- `candidates` is a JSON array (LootCandidatesJson): name, family, wearable,
-- comparison, gain, score, upgrade_percent, item_level_gain, role, class,
-- spec, tank, why.
-- `status`: open (waiting for a decision), decided (a recipient is named, or
-- nobody), given (it reached somebody), lapsed (the roll ended without the
-- council), failed (it could not be handed over to anybody).
-- `decided_by`: jev, both (Jev agreed with the heuristic) or heuristic.
-- `reason` is the sentence the Chronicle and Bags show.
CREATE TABLE IF NOT EXISTS `overseer_loot_council` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `council_key` VARCHAR(48) NOT NULL,
    `kind` VARCHAR(8) NOT NULL,
    `family` VARCHAR(32) NOT NULL DEFAULT '',
    `source` VARCHAR(255) NOT NULL DEFAULT '',
    `map` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `item_entry` INT UNSIGNED NOT NULL,
    `item_name` VARCHAR(255) NOT NULL DEFAULT '',
    `item_quality` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `candidates` TEXT NOT NULL,
    `heuristic` VARCHAR(12) NOT NULL DEFAULT '',
    `heuristic_why` VARCHAR(255) NOT NULL DEFAULT '',
    `status` VARCHAR(8) NOT NULL DEFAULT 'open',
    `recipient` VARCHAR(12) NOT NULL DEFAULT '',
    `reason` VARCHAR(255) NOT NULL DEFAULT '',
    `decided_by` VARCHAR(10) NOT NULL DEFAULT '',
    `given_to` VARCHAR(12) NOT NULL DEFAULT '',
    `item_guid` INT UNSIGNED NOT NULL DEFAULT 0,
    `outcome` VARCHAR(255) NOT NULL DEFAULT '',
    `opened_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `decided_at` TIMESTAMP NULL DEFAULT NULL,
    `given_at` TIMESTAMP NULL DEFAULT NULL,
    PRIMARY KEY (`id`),
    UNIQUE KEY `council_key` (`council_key`),
    KEY `status_opened` (`status`, `opened_at`),
    KEY `item_guid` (`item_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Who a dropped item goes to, and why: the loot council';
