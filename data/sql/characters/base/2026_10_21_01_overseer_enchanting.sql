-- Standing item-target errands for disenchanting and permanent enchanting.
ALTER TABLE `overseer_roster`
    ADD COLUMN `disenchant_guid` BIGINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT 'Owned item GUID for the standing disenchant errand; 0 means none',
    ADD COLUMN `enchant_spell` SMALLINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT 'Permanent enchant spell for the standing enchant errand; 0 means none',
    ADD COLUMN `enchant_slot` TINYINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT 'Equipment slot targeted by enchant_spell';

-- Keep the complete enum union. MODIFY COLUMN replaces the enum, so omitting
-- an existing value would make rows using that value invalid.
ALTER TABLE `overseer_command`
    MODIFY COLUMN `kind` ENUM('bot','chat','gm','probe','give','share','trade','job','sell','bank','auction','mail','repair','buy','bind','hearth','summon','conjure','cast','guild','quest','naturalize','disenchant')
        NOT NULL DEFAULT 'bot';
