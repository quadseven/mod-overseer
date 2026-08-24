-- What the family actually did, written down by the server that watched it
-- happen (infra#2597).
--
-- WHY THIS TABLE EXISTS. Every diagnosis in this epic so far has ended at a
-- screenshot. A repeating "Must have a Axe equipped" cost hours because the
-- worldserver COMPUTED that refusal, sent it to a client, and kept no record.
-- A strategy command reported `delivered` while doing nothing. A council
-- decision was discarded one line short of being persisted. In each case the
-- server held the fact and threw it away, and the only instrument left was a
-- human looking at the game. overseer_event is the opposite posture: if the
-- server knew something happened, there is a row.
--
-- WHY NOT overseer_chat OR overseer_snapshot. Chat is what characters SAID,
-- which is exactly the layer that lied - the family held a coherent
-- conversation about killing kobolds and then stood still. Snapshots are
-- position and vitals sampled every five seconds, so anything that begins and
-- ends between two samples never existed. This is the third shape: discrete
-- things that HAPPENED, at the moment the core decided they had.
--
--
-- ============================ VOLUME ==================================
--
-- This worldserver carries 500 random bots plus the family of five. A row per
-- occurrence would be a firehose from two directions at once, and both were
-- observed, not imagined:
--
--   1. The population. 500 bots dying, levelling, equipping and taking quests
--      is orders of magnitude more traffic than the five characters anybody
--      wants to read about.
--   2. The repeat. The axe error fired about every ten seconds, forever. Six
--      rows a minute, 8,640 a day, from ONE bug on ONE character - and every
--      one of those rows says the identical thing.
--
-- So volume is bounded in four places, and only the last of them is in this
-- file:
--
--   - SCOPE. The module records only characters listed in overseer_roster,
--     which is exactly the five. The 500 are never considered. This is by far
--     the largest reduction and it costs nothing, because nobody has ever
--     wanted a random bot's death in a report.
--   - COALESCING IN MEMORY. The module's pending queue is keyed, not a list,
--     so a repeat increments a counter in RAM instead of appending. A hundred
--     identical failures between two flushes are one row's worth of work.
--   - THE UNIQUE KEY BELOW. Repeats collapse onto one row via ON DUPLICATE
--     KEY UPDATE, carrying an occurrence count and a last_seen.
--   - RETENTION. The module sweeps rows older than a fortnight, the same way
--     it already sweeps overseer_chat.
--
-- WHY COUNT + first_seen/last_seen INSTEAD OF ONE ROW PER OCCURRENCE. The
-- questions actually asked of this data are "is this still happening", "when
-- did it start", and "how bad is it". A count with a first and last sighting
-- answers all three in one row. A row per occurrence answers them no better
-- and buries them: the axe bug would be 8,640 rows a day of identical text,
-- and the reporting layer would have to GROUP BY to get back to the shape
-- that was thrown away on insert.
--
-- WHY A TIME BUCKET AND NOT ONE ROW PER (character, kind, subject) FOREVER.
-- Collapsing forever would flatten the timeline: "Grug died" would be a single
-- row with count=417 and no way to see he died eleven times in one hour
-- yesterday and not since. `bucket` is UNIX time / 3600 - the hour the event
-- fell in - and it is part of the unique key, so a repeating problem produces
-- one row per hour with its count, which IS the timeline at the resolution
-- anyone cares about. One-shot events (a quest accepted, a level gained) carry
-- a distinct subject_id and get their own row regardless of bucketing, so the
-- coarsening only ever applies to things that genuinely repeat.
--
-- The bucket is computed on the WORLDSERVER's clock while first_seen/last_seen
-- come from MySQL's. They are different pods; a second of skew between them
-- changes nothing, because the bucket is a grouping key and never a timestamp
-- anybody reads.
--
--
-- ======================= MIGRATION PATH ==============================
--
-- `kind` IS A VARCHAR AND NOT AN ENUM, DELIBERATELY. This project has been
-- caught twice by exactly that choice: overseer_command.kind needed
-- 2026_08_23_03_overseer_probe.sql to MODIFY the enum before 'probe' could be
-- written, and overseer_goal.kind needed the same treatment. The trap is that
-- the C++ compiles, the INSERT runs, and MySQL either truncates to '' or
-- errors at runtime - so a new event type looks implemented and records
-- nothing. Event kinds are the part of this table most likely to grow (the
-- spell-cast-failure kinds below are already reserved and not yet written), so
-- the column is a plain VARCHAR(32) with a documented vocabulary and the
-- module owns the values. Adding a kind is then a code change and NOT a schema
-- change, which is the whole point.
--
-- Columns are a different matter and DO need a migration. Adding one to this
-- table is a new dated file with an explicit ALTER, exactly like
-- 2026_08_23_01_overseer_roster_lead.sql:
--
--     ALTER TABLE `overseer_event`
--         ADD COLUMN `whatever` INT UNSIGNED NOT NULL DEFAULT 0
--         COMMENT 'why it is here';
--
-- Never by editing this file. AzerothCore's dbimport tracks applied SQL by
-- content hash in the `updates` table, so a re-hashed file is re-applied
-- against a database that already has the table - and this CREATE is written
-- WITHOUT `IF NOT EXISTS` on purpose so that re-application fails loudly at
-- import time instead of silently skipping the change that mattered.
--
-- If a change ever needs the unique key to grow (a new dimension that repeats
-- must be split on), that is two statements in one migration - DROP INDEX then
-- ADD UNIQUE KEY - and it must be thought about, because widening the key
-- splits existing rows' futures and narrowing it cannot merge their pasts.

CREATE TABLE `overseer_event` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,

    -- WHO. Name rather than guid as the key half, because every other overseer
    -- table is keyed by name (overseer_roster.name, overseer_chat.heard_by)
    -- and the bridge speaks names, not guids. The guid rides along for anyone
    -- who needs to join `characters`, and is NOT in the unique key: a
    -- character that is deleted and remade under the same name is, for the
    -- purposes of this log, the same character.
    `character_name` VARCHAR(12) NOT NULL,
    `character_guid` INT UNSIGNED NOT NULL DEFAULT 0,

    -- WHAT. See the vocabulary note above for why this is not an ENUM.
    -- Written by mod-overseer; the values it can currently produce are:
    --
    --   level_up        subject_id = the level just reached
    --                   detail     = 'from <old level>'
    --   quest_accept    subject_id = quest id, subject_name = quest title
    --   quest_complete  subject_id = quest id, subject_name = quest title
    --                   (objectives satisfied - NOT turned in)
    --   quest_reward    subject_id = quest id, subject_name = quest title
    --                   (handed in and rewarded)
    --   item_equip      subject_id = item entry, subject_name = item name
    --                   detail     = 'slot <n>'
    --   death           subject_id = 0
    --
    -- RESERVED, not yet written by any code, and named here so the reporting
    -- layer can be built against them and so nobody invents a second spelling:
    --
    --   cast_fail       subject_id = spell id, subject_name = spell name
    --                   detail     = the SpellCastResult name
    --
    -- The cast-failure kinds are unimplemented because the pinned core has no
    -- hook that can see a failure REASON. AllSpellScript::OnSpellCheckCast is
    -- the only SpellCastResult hook in the whole core, and Spell::CheckCast
    -- calls it at its very top with res already set to SPELL_CAST_OK
    -- (Spell.cpp:5673-5678) - it is an input, a chance to VETO a cast, not a
    -- report of how one went. The reason is computed afterwards and handed
    -- straight to Spell::SendCastResult (Spell.cpp:3546, 3854) with no script
    -- call anywhere between. Recording it needs a core patch, and the C++ in
    -- this repo only compiles on a push to main, so that is deliberately a
    -- separate change rather than a guess bundled into this one.
    `kind` VARCHAR(32) NOT NULL,

    -- WHICH ONE. Kept as a bare id plus a resolved name, rather than JSON,
    -- because every question anyone has asked so far is either "which spell"
    -- or "which quest" and both want an indexed integer. subject_name is the
    -- human's copy and may be empty when the template could not be resolved.
    `subject_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `subject_name` VARCHAR(255) NOT NULL DEFAULT '',

    -- Anything that does not deserve a column of its own. Not part of the
    -- unique key, so the LAST occurrence in a bucket wins - which is what you
    -- want for "which slot was it in this time".
    `detail` VARCHAR(255) NOT NULL DEFAULT '',

    -- WHERE AND WHEN IN THE CHARACTER'S LIFE. Recorded at the moment of the
    -- event because that is precisely what the `characters` table cannot tell
    -- you: PlayerSaveInterval is 900000, so a level read from the database can
    -- be a quarter of an hour behind the thing that caused it.
    `level` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `map` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `zone` INT UNSIGNED NOT NULL DEFAULT 0,

    -- The hour this row covers: UNIX time / 3600, from the worldserver clock.
    `bucket` INT UNSIGNED NOT NULL DEFAULT 0,

    -- NOT NAMED `count`. COUNT is a function name, and while MySQL 8 will
    -- accept it as an unquoted column here, this project has already taken the
    -- worldserver down in a crash loop over `lead` (the LEAD() window
    -- function) in 2026_08_23_01_overseer_roster_lead.sql. A column that never
    -- needs a backtick cannot be the reason a query is a syntax error at three
    -- in the morning.
    `occurrences` INT UNSIGNED NOT NULL DEFAULT 1,

    `first_seen` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `last_seen` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (`id`),

    -- The whole de-duplication contract, in one line. The module INSERTs with
    -- ON DUPLICATE KEY UPDATE against this key, so a repeat is an increment.
    UNIQUE KEY `uq_event` (`character_name`, `kind`, `subject_id`, `bucket`),

    -- "What has happened lately", the query the reporting layer will actually
    -- run, and "what is this character up to", the one a person will.
    KEY `idx_last_seen` (`last_seen`),
    KEY `idx_character_last_seen` (`character_name`, `last_seen`),
    KEY `idx_kind_last_seen` (`kind`, `last_seen`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Discrete things that happened to roster characters, de-duplicated per hour';
