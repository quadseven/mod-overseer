-- What the bridge reads of a refused or long errand walk (mod-overseer#1498).
--
-- The travel refusal row already names the refused aim, the reason and when it was
-- refused. The bridge picks an errand's aim by straight-line distance and cannot
-- read the planned route, so the same row now also says when the refusal ends and
-- how many times longer the last planned walk is than the straight line to its aim.
-- Times are Unix seconds. Zero means nothing recorded.
-- Re-applyable: dbimport may run this file again, so each column is added only
-- when it is missing.

SET @add_refused_until = IF(
    (SELECT COUNT(*) = 0
       FROM INFORMATION_SCHEMA.COLUMNS
      WHERE TABLE_SCHEMA = DATABASE()
        AND TABLE_NAME = 'overseer_travel_refusal'
        AND COLUMN_NAME = 'refused_until'),
    "ALTER TABLE `overseer_travel_refusal` ADD COLUMN `refused_until` BIGINT NOT NULL DEFAULT 0 AFTER `refused_at`",
    "SELECT 1"
);
PREPARE add_refused_until_stmt FROM @add_refused_until;
EXECUTE add_refused_until_stmt;
DEALLOCATE PREPARE add_refused_until_stmt;

SET @add_route_ratio = IF(
    (SELECT COUNT(*) = 0
       FROM INFORMATION_SCHEMA.COLUMNS
      WHERE TABLE_SCHEMA = DATABASE()
        AND TABLE_NAME = 'overseer_travel_refusal'
        AND COLUMN_NAME = 'route_ratio'),
    "ALTER TABLE `overseer_travel_refusal` ADD COLUMN `route_ratio` FLOAT NOT NULL DEFAULT 0 AFTER `struck_at`",
    "SELECT 1"
);
PREPARE add_route_ratio_stmt FROM @add_route_ratio;
EXECUTE add_route_ratio_stmt;
DEALLOCATE PREPARE add_route_ratio_stmt;

SET @add_route_ratio_target = IF(
    (SELECT COUNT(*) = 0
       FROM INFORMATION_SCHEMA.COLUMNS
      WHERE TABLE_SCHEMA = DATABASE()
        AND TABLE_NAME = 'overseer_travel_refusal'
        AND COLUMN_NAME = 'route_ratio_target'),
    "ALTER TABLE `overseer_travel_refusal` ADD COLUMN `route_ratio_target` VARCHAR(255) NOT NULL DEFAULT '' AFTER `route_ratio`",
    "SELECT 1"
);
PREPARE add_route_ratio_target_stmt FROM @add_route_ratio_target;
EXECUTE add_route_ratio_target_stmt;
DEALLOCATE PREPARE add_route_ratio_target_stmt;
