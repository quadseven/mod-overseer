-- One row per family leader: what the module's intent book holds him for, the
-- requests on the table, and Jev's pick for the family (wow-overseer#335
-- follow-up).
--
-- TWO WRITERS, DISJOINT COLUMNS. The module writes the `current_*`,
-- `on_the_table`, `changes`, `goal_yards`, `members_state` and `module_at`
-- columns on every party poll (30 s) with INSERT ... ON DUPLICATE KEY UPDATE
-- of those columns only. The bridge writes the `chosen_*` columns with an
-- UPDATE of those columns only. Neither side ever writes the other's, so
-- neither can erase the other's answer.
--
-- `on_the_table` is one request per line, `kind|owner|target`, highest rank
-- first: everything a rule asked for this leader in the last 75 seconds,
-- granted or not. It is the list of legal intents the bridge offers Jev.
--
-- `goal_yards` is how far the leader stands from the place his travel column
-- resolved to, when it resolved to one on his map: a role errand ('banker')
-- has no coordinates the bridge can read, and Jev was asked about "goal
-- banker ?" with no distance at all. `members_state` is one member per line,
-- `name|state|yards`: what the module is doing with each of them (walking
-- back, held far from the leader, held off a deadly walk, following) and how
-- far they stand from the leader - the holds and walks the bridge could not
-- see before.
--
-- The module honours a pick only while `chosen_by` is 'jev' and `chosen_until`
-- is in the future, and only by ranking that request above the heuristic
-- rules' own; the campaign's run and an operator order still come first.
CREATE TABLE IF NOT EXISTS `overseer_family_intent` (
    `leader_name`       VARCHAR(12)   NOT NULL,
    `family`            VARCHAR(32)   NOT NULL DEFAULT '',
    `current_kind`      VARCHAR(16)   NOT NULL DEFAULT 'none',
    `current_owner`     VARCHAR(32)   NOT NULL DEFAULT '',
    `current_target`    VARCHAR(96)   NOT NULL DEFAULT '',
    `current_since`     TIMESTAMP     NULL DEFAULT NULL,
    `on_the_table`      VARCHAR(1000) NOT NULL DEFAULT '',
    `changes`           INT UNSIGNED  NOT NULL DEFAULT 0,
    `goal_yards`        INT UNSIGNED  NULL DEFAULT NULL,
    `members_state`     VARCHAR(1000) NOT NULL DEFAULT '',
    `module_at`         TIMESTAMP     NULL DEFAULT NULL,
    `chosen_kind`       VARCHAR(16)   NOT NULL DEFAULT '',
    `chosen_target`     VARCHAR(96)   NOT NULL DEFAULT '',
    `chosen_by`         VARCHAR(16)   NOT NULL DEFAULT '',
    `chosen_confidence` FLOAT         NULL DEFAULT NULL,
    `chosen_until`      TIMESTAMP     NULL DEFAULT NULL,
    PRIMARY KEY (`leader_name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
