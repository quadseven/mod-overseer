-- A dungeon run is a THING, with a beginning, an owner and a heartbeat
-- (mod-overseer#88, slice 2).
--
-- WHY THIS TABLE EXISTS. Slice 1 gave every steering drive one predicate to
-- stand down on, and three independent reviews of it landed on the same defect:
-- the predicate answered "is this character standing on an instance map", which
-- is NOT the same question as "does an active run own this character". One
-- reviewer put it exactly: "A character can be in a dungeon without an active
-- run, and this code will still stand down all drives even though no run exists
-- to arbitrate. This conflates 'in dungeon' with 'in active run'."
--
-- That is not pedantry. A character teleported inside with no run in progress
-- would have had EVERY drive stand down on it, and nothing would have picked it
-- back up - which is the exact stranding shape this codebase has now hit four
-- separate times (#79, #85, #87, and the corpse case before them). Inferring
-- ownership from geography produces a state nothing owns.
--
-- So ownership becomes a row. A run is opened when a roster character is first
-- seen inside an instance, and closed when none remain. `InDungeonRun` then
-- asks the run, not the map.
--
-- WHY last_progress_at IS HERE NOW, BEFORE ANYTHING READS IT. Every review also
-- asked for a watchdog keyed on PROGRESS rather than on state, because every
-- stall observed so far has been a character in a perfectly valid-looking state
-- that could never advance. That watchdog is a later slice, but the column it
-- needs is cheap to carry from the start and expensive to backfill later, so it
-- is written from the first tick even though nothing reads it yet.
--
-- WHY THE LEADER IS A NAME AND NOT A GUID. Every other overseer table keys on
-- the character name (overseer_roster, overseer_command, overseer_death), the
-- roster is five hand-named characters, and a name survives a character being
-- recreated during development in a way a guid does not.

CREATE TABLE IF NOT EXISTS `overseer_dungeon_run` (
  `id`               INT UNSIGNED     NOT NULL AUTO_INCREMENT,
  `leader_name`      VARCHAR(24)      NOT NULL,
  `map_id`           SMALLINT UNSIGNED NOT NULL,
  `state`            ENUM('active','ended') NOT NULL DEFAULT 'active',
  `started_at`       TIMESTAMP        NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `last_progress_at` TIMESTAMP        NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `ended_at`         TIMESTAMP        NULL DEFAULT NULL,
  `ended_reason`     VARCHAR(200)     NOT NULL DEFAULT '',
  -- ONE ACTIVE RUN PER MAP, ENFORCED BY THE DATABASE RATHER THAN BY A CHECK.
  --
  -- The first draft of this read "is there an active run" and then inserted if
  -- not, which a review caught immediately: two characters walking through the
  -- portal in the same tick both see no run and both insert, and the invariant
  -- the whole slice rests on is gone. A SELECT-then-INSERT is not a claim, it
  -- is a hope.
  --
  -- MySQL has no partial index, so uniqueness is carried on a generated column
  -- that holds the map id ONLY while the run is active and NULL once it ends.
  -- NULLs do not collide in a unique index, so any number of ended runs may
  -- share a map while at most one active run ever can. The insert is then
  -- INSERT ... ON DUPLICATE KEY UPDATE, which is atomic: the loser of the race
  -- touches the heartbeat instead of creating a rival run.
  `active_map` SMALLINT UNSIGNED GENERATED ALWAYS AS
      (IF(`state` = 'active', `map_id`, NULL)) STORED,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uniq_active_map` (`active_map`),
  -- The lookup every tick performs: "is there an active run on this map".
  KEY `idx_active_map` (`state`, `map_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
