-- kind='portal': family mage portal network (infra#XXXX)
--
-- A mage in the family opens a portal to a major city on command. The
-- operator or any family member can request a portal; the module finds
-- the appropriate mage (Alliance or Horde), ensures it knows the spell,
-- and triggers the cast through the existing steering mechanism.
--
-- WHY A NEW KIND. The cast verb already exists and takes a spell id, but
-- a portal request is a higher-level concept: "open a portal to Stormwind"
-- rather than "cast spell 10059". The city name is portable across
-- characters and factions; the spell id is not. A portal row also
-- coordinates the two family mages (Og on Alliance, Oz on Horde) so the
-- requester does not need to know which mage serves their city.
--
-- THE COMMAND GRAMMAR.
--
--   portal <city>
--
-- City names accepted (case-insensitive):
--   Alliance: stormwind, ironforge, darnassus
--   Horde:    orgrimmar, thunder bluff, undercity
--   Neutral:  shattrath, dalaran
--
-- The module maps the city to the correct mage and the correct spell
-- (Portal or Teleport, preferring Portal when available). If the mage
-- does not know the spell, the row is refused with a hint to train it
-- via the existing 'cast walk-to-trainer class' mechanism.
--
-- THE ENUM LISTS THE FULL UNION. An ALTER that MODIFYs an ENUM replaces
-- the whole list, so every kind ever added must appear here.
ALTER TABLE `overseer_command`
    MODIFY COLUMN `kind` ENUM('bot','chat','gm','probe','give','share','trade','job','sell','bank','auction','mail','repair','buy','bind','hearth','summon','conjure','cast','guild','quest','naturalize','portal')
        NOT NULL DEFAULT 'bot';

-- THE PORTAL REGISTRY. One row per city the family can portal to,
-- recording which mage owns it and which spell creates it. The bridge
-- populates this from the mage's spellbook; the module reads it to
-- resolve a city name to a caster and spell id.
CREATE TABLE IF NOT EXISTS `overseer_portal` (
    `city` VARCHAR(32) NOT NULL,
    `faction` ENUM('Alliance','Horde','Neutral') NOT NULL,
    `caster_name` VARCHAR(12) NOT NULL,
    `spell_id` INT UNSIGNED NOT NULL,
    `spell_name` VARCHAR(64) NOT NULL,
    `is_portal` TINYINT(1) NOT NULL DEFAULT 1 COMMENT '1 = Portal (creates object), 0 = Teleport (moves caster)',
    PRIMARY KEY (`city`),
    KEY `idx_caster` (`caster_name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Family mage portal registry: city -> caster + spell';

-- Seed data for the two family mages (Og Alliance, Oz Horde).
-- Spell IDs from 3.3.5a DBC. Portal spells create a gameobject; Teleport spells move the caster.
-- The bridge should update this table when mages learn new ranks.
INSERT IGNORE INTO `overseer_portal` (`city`, `faction`, `caster_name`, `spell_id`, `spell_name`, `is_portal`) VALUES
    -- Alliance portals (Og)
    ('stormwind', 'Alliance', 'Og', 10059, 'Portal: Stormwind', 1),
    ('ironforge', 'Alliance', 'Og', 11416, 'Portal: Ironforge', 1),
    ('darnassus', 'Alliance', 'Og', 11419, 'Portal: Darnassus', 1),
    ('stormwind', 'Alliance', 'Og', 3561, 'Teleport: Stormwind', 0),
    ('ironforge', 'Alliance', 'Og', 3562, 'Teleport: Ironforge', 0),
    ('darnassus', 'Alliance', 'Og', 3565, 'Teleport: Darnassus', 0),
    -- Horde portals (Oz)
    ('orgrimmar', 'Horde', 'Oz', 11417, 'Portal: Orgrimmar', 1),
    ('thunder bluff', 'Horde', 'Oz', 11420, 'Portal: Thunder Bluff', 1),
    ('undercity', 'Horde', 'Oz', 11418, 'Portal: Undercity', 1),
    ('orgrimmar', 'Horde', 'Oz', 3563, 'Teleport: Orgrimmar', 0),
    ('thunder bluff', 'Horde', 'Oz', 3566, 'Teleport: Thunder Bluff', 0),
    ('undercity', 'Horde', 'Oz', 3567, 'Teleport: Undercity', 0),
    -- Neutral portals (both mages can learn)
    ('shattrath', 'Neutral', 'Og', 33690, 'Portal: Shattrath', 1),
    ('shattrath', 'Neutral', 'Oz', 33690, 'Portal: Shattrath', 1),
    ('dalaran', 'Neutral', 'Og', 53142, 'Portal: Dalaran', 1),
    ('dalaran', 'Neutral', 'Oz', 53142, 'Portal: Dalaran', 1);