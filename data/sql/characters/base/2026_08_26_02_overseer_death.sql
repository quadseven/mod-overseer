-- A death recorded with the context that explains it, at the moment it
-- happens (infra#2912, part of infra#2912's parent epic).
--
-- WHY THIS TABLE EXISTS AND overseer_event IS NOT ENOUGH. Twice on 2026-08-26
-- the family drifted into a lethal high-level zone - Searing Gorge, then
-- Burning Steppes - with no quest or travel aim pointed there, and both times
-- the only instrument available afterwards was `acore_characters`, saved
-- every fifteen minutes (PlayerSaveInterval=900000). By the time anyone
-- looked, the state that would explain the drift - what the character had
-- been doing in its last seconds, what killed it, whether its health had
-- been dropping for a while or it went from full to zero in one hit - was
-- already gone. `overseer_event`'s 'death' kind (2026_08_24_02) cannot fill
-- this gap by design: it is an HOURLY-BUCKETED COUNT with no killer, no
-- health trend and no aim, built to answer "is this still happening" for a
-- 500-bot world where a firehose of identical rows would be worse than none.
-- This table asks a different question - "what happened THIS time" - so it
-- is deliberately the opposite shape: one row per death, never coalesced,
-- never updated after the fact.
--
-- WHY EVERY COLUMN IS HERE, NOT A SUBSET.
--
--   killer_type / killer_name / killer_entry
--       "Why do they enter zones far above their level" and "does flee work"
--       both start with knowing who or what did the killing. killer_entry is
--       the creature TEMPLATE id (Magma Elemental is one entry however many
--       times it is tapped), so "which mob keeps killing this cohort" is a
--       GROUP BY away rather than a text match on a name that can collide.
--       killer_type is 'creature', 'player', or 'environment' - the last
--       covers fall damage, drowning, fatigue, lava and anything else that
--       reaches Unit::Kill with a null killer, and is written explicitly
--       rather than left as an empty string so it reads as an answer, not a
--       gap the reporting layer has to interpret.
--
--   health_at_death / max_health_at_death / seconds_since_full_health
--       The epic's own acceptance criterion: "health over the last N seconds
--       ... what killed them". mod_overseer.cpp cannot read live health at
--       the point a death hook fires - it is already zero by then, verified
--       against the pinned core (see the death-context comment above
--       OverseerEventScript in mod_overseer.cpp) - so these three columns
--       hold the last SAMPLED reading and how long ago it was full, not the
--       live value. "Went from full health to dead between two five-second
--       samples" and "was below half health for two minutes first" are both
--       answerable from these three numbers and were not answerable at all
--       before this table.
--
--   job / quest_aim / travel_target
--       "What was steering the character" - the first of the epic's open
--       questions ("no quest or travel aim pointed at Searing Gorge... the
--       drift mechanism is unknown"). Recorded as mod_overseer.cpp's OWN
--       DRIVES last read them (RememberAim, called from DriveQuests every
--       QUEST_POLL_MS), not re-queried at death time - see GUARDED READS
--       below for why a fresh query at this exact moment is the one thing
--       this table must never do.
--
--   grouped / group_size / group_leader
--       "What does a group actually buy them" - the epic's fourth open
--       question, stated as "they were five and it did not save them".
--       Answerable per-death instead of asserted from memory.
--
-- GUARDED READS, GUARDED WRITES - THE SAME DISCIPLINE AS EVERY LATE COLUMN IN
-- THIS MODULE, APPLIED TO A WHOLE TABLE. mod-overseer's SQL is applied by the
-- db-upgrade initContainer running the DB-IMPORT image; mod_overseer.cpp is
-- compiled into the WORLDSERVER image; the two are pinned by separate
-- digests in the same manifest and bumped independently (30-db-import.yaml:
-- "only db-import gets `COPY data data`"). A worldserver that starts before
-- db-import has run this migration would find no `overseer_death` table at
-- all. RecordDeath (the hook-side capture) never touches the database, so
-- that ordering cannot touch the death path itself; FlushDeaths (the
-- world-thread writer) calls CharacterDatabase.Execute exactly the way
-- FlushEvents and FlushChat already do for their own tables, which log and
-- move on rather than throw when a statement fails - so a worldserver ahead
-- of its own schema loses death ROWS until db-import catches up, and nothing
-- else. The reverse ordering (this migration applied, worldserver still
-- running the previous image) is also safe: the table sits empty until the
-- new image ships. Neither ordering can crash or block Player::KillPlayer.
--
-- WHY THIS IS A CREATE AND NOT PART OF overseer_event. Different shape
-- (never coalesced, no unique key to collapse repeats onto) and a different
-- lifecycle (see DEATH_RETENTION_DAYS in mod_overseer.cpp - deliberately
-- independent of EVENT_RETENTION_DAYS so the two can be tuned separately
-- once there is a cohort's worth of rows to look at). Written WITHOUT
-- `IF NOT EXISTS`, matching overseer_event's own migration, so that
-- re-applying this file against a database that already has the table fails
-- loudly at import time instead of silently skipping a change that mattered
-- - dbimport tracks applied files by content hash in `updates`, so editing
-- this file later rather than adding a new dated ALTER would otherwise be a
-- silent no-op against every database that already ran it once.
--
-- OUT OF SCOPE, ON PURPOSE (infra#2912 is one slice of the epic infra#2912
-- describes). This table and its C++ reader ship in mod-overseer, the same
-- module every realm's worldserver already runs - nothing here is scoped to
-- a namespace, a MySQL instance, or a realm id, because which characters
-- show up in it is entirely a function of which `overseer_roster` the
-- worldserver that wrote the row was reading. Turning it on for the live
-- `wow` family - or wiring up the isolated hardcore realm this was actually
-- built for - is a deployment decision for a separate change, not a schema
-- one.

CREATE TABLE `overseer_death` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,

    -- WHO. Same convention as overseer_event: name is the join key every
    -- other overseer table and the bridge use, guid rides along for anyone
    -- joining to `characters`.
    `character_name` VARCHAR(12) NOT NULL,
    `character_guid` INT UNSIGNED NOT NULL DEFAULT 0,

    -- WHERE AND WHAT LEVEL. Recorded at the moment of death, not read back
    -- from `characters` (up to fifteen minutes stale - the exact problem
    -- this table exists to fix).
    `level` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `map` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `zone` INT UNSIGNED NOT NULL DEFAULT 0,
    `pos_x` FLOAT NOT NULL DEFAULT 0,
    `pos_y` FLOAT NOT NULL DEFAULT 0,
    `pos_z` FLOAT NOT NULL DEFAULT 0,

    -- WHAT KILLED THEM. VARCHAR and not ENUM for killer_type - this project
    -- has been caught twice already needing a migration just to widen an enum
    -- before a value could be written (overseer_command.kind, overseer_goal.
    -- kind), and this column can only ever hold three plain-English strings:
    -- 'creature', 'player', 'environment'. killer_name is sized for a
    -- creature TEMPLATE name ("Magma Elemental" is 15 characters, longer than
    -- the 12-character player-name columns everywhere else in this schema),
    -- not a character name. killer_entry is 0 for a player killer or an
    -- environmental death - only a creature has a template id to group on.
    `killer_type` VARCHAR(16) NOT NULL DEFAULT 'environment',
    `killer_name` VARCHAR(100) NOT NULL DEFAULT '',
    `killer_entry` INT UNSIGNED NOT NULL DEFAULT 0,

    -- HEALTH TREND. The LAST SAMPLED reading before death, not the live value
    -- at the point any death hook fires - by then it is already zero on every
    -- vantage point the pinned core offers (see this migration's own header
    -- comment). `seconds_since_full_health` is 0 when the cache never saw
    -- this character at full health at all (e.g. it died within the first
    -- sample window after login) - indistinguishable from "died instantly
    -- after healing", which is the honest answer for both.
    `health_at_death` INT UNSIGNED NOT NULL DEFAULT 0,
    `max_health_at_death` INT UNSIGNED NOT NULL DEFAULT 0,
    `seconds_since_full_health` INT UNSIGNED NOT NULL DEFAULT 0,

    -- WHAT WAS STEERING IT. Mirrors overseer_roster's own columns and their
    -- own "no opinion" sentinels exactly (2026_08_24_00_overseer_roster_
    -- drive_quest.sql, 2026_08_25_00_overseer_roster_travel_npc.sql,
    -- 2026_08_26_01_overseer_roster_job.sql) - 0 / '' / 'quest' mean the same
    -- thing here as they do there, and a reporting query never has to learn a
    -- second vocabulary for the same fact.
    `job` VARCHAR(20) NOT NULL DEFAULT 'quest',
    `quest_aim` INT UNSIGNED NOT NULL DEFAULT 0,
    `travel_target` VARCHAR(32) NOT NULL DEFAULT '',

    -- WHAT A GROUP BOUGHT THEM.
    `grouped` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `group_size` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `group_leader` VARCHAR(12) NOT NULL DEFAULT '',

    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    PRIMARY KEY (`id`),

    -- "What has this character been dying to lately" and "what is killing
    -- this cohort" are the two questions the epic's acceptance criteria
    -- actually ask - a cohort comparison group-bys killer_entry across a time
    -- window, a single-character diagnosis filters on character_name.
    KEY `idx_character_created` (`character_name`, `created_at`),
    KEY `idx_killer` (`killer_type`, `killer_entry`, `created_at`),
    KEY `idx_zone_created` (`zone`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='One row per roster-character death, with the context acore_characters is too stale to hold (infra#2912)';
