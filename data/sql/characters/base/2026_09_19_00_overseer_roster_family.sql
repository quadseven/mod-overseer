-- Which cohort a roster row belongs to (infra#4221). One column, one table,
-- so that a second guild can share this machinery instead of forking it.
--
-- THE TABLE HAS NEVER HAD A CONCEPT OF WHICH GROUP A ROW IS IN. Every row in
-- `overseer_roster` has been one family's row since the table was created
-- (2026_08_23_00), and every family-wide decision on both sides of the wire
-- reads the whole table on that assumption: the module's own
-- `SELECT name FROM overseer_roster WHERE enabled = 1` queries, and about
-- nineteen equivalent reads in the bridge. That assumption is true today and
-- costs nothing. It stops being true the moment a second group of characters
-- gets a row here, and the read-only audit on infra#4221 measured what that
-- would cost: the functions that require the whole roster to AGREE on
-- something (one job, one standing mode, one training state) return "no
-- agreement" the instant two groups disagree, and every drive gated on them
-- stands down silently - for the ORIGINAL family too, not just the new one.
--
-- THIS IS THE OPPOSITE DECISION TO THE ONE infra#4110 MADE, ON PURPOSE. That
-- epic also wanted a second group of characters, and it kept them OUT of this
-- table entirely: no roster row, no guild, no family professions, no family
-- streams, read-only and isolated. That worked because that product wanted
-- almost nothing from the shared machinery - it was five characters duelling
-- each other. The goal this column serves wants the reverse: a second guild
-- with FULL parity - jobs, travel, professions, guild bank, auctions - so
-- isolation is not available this time. Either the table learns which cohort
-- a row belongs to, or the schema and the ~19 call sites get duplicated per
-- cohort and drift apart silently, which is the shape of bug this project
-- keeps paying for elsewhere.
--
-- THIS FILE ADDS THE COLUMN AND NOTHING READS IT YET. That is the whole of
-- this change, deliberately. Scoping the family-wide queries, and fixing the
-- three roster WRITES the audit found with no cohort bound at all (one of
-- them has no WHERE clause of any kind), is the rest of infra#4221 and is not
-- attempted here. The column has to exist in every database first, for the
-- same reason the reader for it cannot ship before the DDL does - see WHY THE
-- C++ SIDE IS UNCHANGED, below.
--
-- WHY THE DEFAULT PRESERVES TODAY'S BEHAVIOUR EXACTLY, IN TWO PARTS, AND THE
-- FIRST PART IS THE ONE THAT ACTUALLY CARRIES THE RISK.
--
-- First: nothing reads this column, so no value it could hold can change what
-- anything does. Every roster SELECT in src/mod_overseer.cpp names its columns
-- explicitly - there is no `SELECT *` against this table anywhere in this
-- module - so adding a column is invisible to all of them, and the bridge's
-- own `INSERT IGNORE INTO overseer_roster (name, note)` names its columns too.
-- The safety of this migration does not rest on which literal is chosen below.
--
-- Second, and this is what the literal IS for: NOT NULL with a DEFAULT rather
-- than a nullable column. The ALTER backfills every existing row in one pass,
-- and every future INSERT that does not name this column lands in the same
-- cohort as every row already there. That is exactly today's single-cohort
-- behaviour, preserved by construction rather than by a backfill statement
-- somebody has to remember to run. A nullable column would invent a third
-- state - "belongs to no cohort" - that every scoped query written later would
-- have to handle, and the first one that forgot would quietly drop live rows
-- out of their own family.
--
-- WHY THIS PARTICULAR LITERAL. The only cohort identity that exists in code
-- today is the head of the family, and it is not in any database: it is
-- derived in `production/scripts/wow-overseer/bonds.py` from the family table
-- there, as the most senior member (seniority 100, the father, 'Grug').
-- Naming a cohort after its head means the scoping work that follows can
-- compute its key from the one place those relationships are already written
-- down, instead of introducing a second constant that can disagree with it.
--
-- AND THE CAVEAT THAT GOES WITH A LITERAL, WRITTEN DOWN NOW RATHER THAN
-- DISCOVERED LATER. A validation world renames the cast, so the head of the
-- family there is not spelled the same way. This DEFAULT is a literal and
-- knows nothing about that. It is still safe - every row in such a database
-- gets the SAME single value, so that database is still a one-cohort database
-- and behaves exactly as it does today - but it means a scoped query must
-- resolve its cohort key from the roster, or through the same rename, and must
-- never hardcode the literal below. The column says which rows go together; it
-- does not promise what that group is called in a world that renames.
--
-- WHY VARCHAR(24) AND NOT SOMETHING NARROWER. #429 is the reason, and it cost
-- a real failure: `job` was sized for the vocabulary it had, the vocabulary
-- grew, and MySQL REFUSED the longer values rather than truncating them -
-- ERROR 1406, and a refused UPDATE is not a partial write, it is nothing
-- happening at all, which the module cannot see and which looks exactly like
-- the row simply keeping what it had. Both vocabularies this column could ever
-- hold are already capped, and both caps are known: a character name is
-- VARCHAR(12) in this table's own CREATE TABLE, and a guild name is 24,
-- enforced by the core itself (GUILD_NAME_MAX in src/overseer_decisions.h,
-- taken from the core's own ObjectMgr). 24 holds either one. A width between
-- the two would hold a family name and refuse a guild name, which is #429
-- again with different characters in it, and a VARCHAR's declared length costs
-- nothing in storage for shorter values.
--
-- WHY THE C++ SIDE IS UNCHANGED IN THIS SAME CHANGE, which is the reverse of
-- the usual worry rather than an omission. The usual worry - spelled out above
-- LoadQuestAims in src/mod_overseer.cpp, and in the `job` and `travel_npc`
-- migrations - is a READER naming a column the table does not have: the DDL
-- and the reader ship in two different images, pinned and bumped
-- independently, and a SELECT naming a missing column fails WHOLE (error 1054,
-- handed back as a null QueryResult that is indistinguishable from "no rows").
-- That is the argument for shipping the column FIRST and its reader after,
-- never the other way round. Here there is no reader at all yet: no query in
-- this module names `family`, and adding a column changes nothing about a
-- query that lists the columns it wants. When a reader for this column does
-- arrive it must read it on its own and treat an unreadable column as the
-- default, exactly as DriveQuests already does for `job` - so that a
-- worldserver older than this file keeps today's behaviour instead of freezing
-- the family.
--
-- THE GUARD, AND WHY IT IS NOT OPTIONAL. AzerothCore's updater hashes this
-- whole FILE, comments included. A later commit that only edits a comment up
-- here changes the hash, and the updater's response to "the file changed" is
-- to run it again - which has already happened once for real to the `job`
-- column (infra#2912 background). An unconditional ADD COLUMN is not safe to
-- run twice: against any database that already has the column, from a restore
-- or from the original apply, it is a duplicate-column error that crash-loops
-- db-import. The guard below makes the second apply execute `SELECT 1` and
-- change nothing.
--
-- NOT `ADD COLUMN IF NOT EXISTS`, which reads like the obvious answer and is
-- not available: it is a flat parse error (ERROR 1064) on this pipeline's
-- pinned MySQL 8.4.11, confirmed against that exact version rather than
-- inferred from documentation. The portable INFORMATION_SCHEMA count plus
-- PREPARE/EXECUTE shape below depends on nothing beyond standard dynamic SQL.
-- Two newer roster columns in this directory (`craft_spell`, `learn_fishing`)
-- are plain unguarded ALTERs and are NOT the pattern to copy; bringing those
-- under the same guard is its own change and is not folded in here.
SET @family_column_missing = (
    SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = 'overseer_roster'
      AND COLUMN_NAME = 'family'
);
SET @add_family_column = IF(
    @family_column_missing,
    "ALTER TABLE `overseer_roster` ADD COLUMN `family` VARCHAR(24) NOT NULL DEFAULT 'Grug' COMMENT 'Which cohort this row belongs to (infra#4221), named after the head of that family. Nothing reads it yet: the family-wide queries on both sides are still unscoped, and scoping them is the rest of infra#4221. Every existing row backfills to the one cohort that exists today, so behaviour is unchanged until a reader arrives.'",
    "SELECT 1"
);
PREPARE add_family_column_stmt FROM @add_family_column;
EXECUTE add_family_column_stmt;
DEALLOCATE PREPARE add_family_column_stmt;
