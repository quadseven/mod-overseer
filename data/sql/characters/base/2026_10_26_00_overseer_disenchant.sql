-- Add the disenchant command without dropping any of the twenty existing
-- command kinds. ALTER ... MODIFY replaces the complete ENUM definition.
ALTER TABLE `overseer_command`
    MODIFY COLUMN `kind` ENUM('bot','chat','gm','probe','give','share','trade','job','sell','bank','auction','mail','repair','buy','bind','hearth','summon','conjure','cast','guild','disenchant')
        NOT NULL DEFAULT 'bot';
