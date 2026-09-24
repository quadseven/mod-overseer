-- overseer_keep: an item a character must keep, whatever else wants it.
--
-- The operator reserves an item on a character: one instance by item_guid, or
-- every instance of item_entry when item_guid is 0. Nothing this module does
-- may then sell it, destroy it, give it, trade it, mail it, auction it or put
-- it in a guild bank, and the core's own sell, mail and trade handlers refuse
-- it too (OverseerKeepScript).
--
-- until_level says where it waits. Below that level it is not worn (the
-- character cannot use it): it is taken off at once and banked in the
-- character's own bank the next time a banker is in reach. At or above the
-- level it is taken out of the bank at a banker and worn again. 0 means it is
-- kept wherever it is and never moved.
--
-- Rows are written by the operator; the module only reads them. A reservation
-- is removed by deleting its row.
CREATE TABLE IF NOT EXISTS `overseer_keep` (
    `character_name` VARCHAR(12)      NOT NULL,
    `item_entry`     INT UNSIGNED     NOT NULL DEFAULT 0,
    `item_guid`      INT UNSIGNED     NOT NULL DEFAULT 0,
    `until_level`    TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `reason`         VARCHAR(255)     NOT NULL DEFAULT '',
    `created_at`     TIMESTAMP        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`character_name`, `item_entry`, `item_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
