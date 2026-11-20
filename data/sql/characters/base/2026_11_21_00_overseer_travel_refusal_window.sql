-- What the bridge reads of a refused or long errand walk (mod-overseer#1498).
--
-- The travel refusal row already names the refused aim, the reason and when it was
-- refused. The bridge picks an errand's aim by straight-line distance and cannot
-- read the planned route, so the same row now also says when the refusal ends and
-- how many times longer the last planned walk is than the straight line to its aim.
-- Times are Unix seconds. Zero means nothing recorded.
ALTER TABLE `overseer_travel_refusal`
    ADD COLUMN `refused_until` BIGINT NOT NULL DEFAULT 0 AFTER `refused_at`,
    ADD COLUMN `route_ratio` FLOAT NOT NULL DEFAULT 0 AFTER `struck_at`,
    ADD COLUMN `route_ratio_target` VARCHAR(255) NOT NULL DEFAULT '' AFTER `route_ratio`;
