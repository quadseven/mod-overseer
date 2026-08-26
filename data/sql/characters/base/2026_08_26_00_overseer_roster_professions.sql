-- The trades a character is meant to hold, and the one it is being sent to
-- settle right now (infra#2757).
--
-- WHAT WAS MISSING. The decision has existed for days and works: professions.py
-- holds the family's assignment with its reasoning, `professions.plan()` picks
-- one trade to open at a time, and `overseer_trade` already carries the answer.
-- Measured live 2026-08-26, straight out of that table:
--
--     id 1  Og  unlearn  alchemy   171  status 'planned'
--     id 2  Og  learn    tailoring 197  status 'planned'
--
-- Both rows correct, both rows two days old, and neither had moved a muscle -
-- because nothing in the worldserver could see them. `overseer_trade` is a
-- Python table; mod-overseer reads `overseer_roster`. The plan was written and
-- unread, which is #2776 again in a new costume.
--
-- These four columns are the road between the two. The bridge writes them off
-- the outstanding `overseer_trade` row; the module reads them, acts, and clears
-- the ones it consumed. The existing settle path is untouched: the bridge still
-- proves an assignment by OBSERVING `character_skills` (professions.settled),
-- never by trusting a column that says the module tried.
--
-- WHY SKILL IDS AND NOT NAMES, having gone the other way for travel_npc. A
-- travel role is a HUMAN decision that a council states in words, so
-- travel.ROLES is a shared vocabulary and tests/test_travel_npc.py compares the
-- two copies line by line to stop them drifting. This is not that. Both ends
-- already hold the same numbers - goals.SKILL_IDS on one side, SkillLineStore
-- and character_skills.skill on the other - so a word here would be a THIRD
-- spelling of a fact that already exists twice, and the only thing a third
-- spelling can add is a way to disagree. The module prints the DBC's own name
-- next to the id in every log line, so nothing is unreadable at 2am.
ALTER TABLE `overseer_roster`
    -- THE DECLARATIVE END STATE, and the module's only permission to act.
    --
    -- The primary professions this character is MEANT to hold, as
    -- character_skills.skill ids, comma separated, e.g. '197,333' for Og. The
    -- source is professions.ROSTER, which keeps the sentence explaining each
    -- choice next to it - that prose has no place in a VARCHAR and is not
    -- duplicated here.
    --
    -- WHY THIS IS A GATE AND NOT A HINT. `learn_skill` and `unlearn_skill`
    -- below are instructions, and an instruction can be wrong: a stale row, a
    -- half-finished migration, somebody editing the table by hand at 2am. This
    -- column is what stops a wrong instruction being obeyed. The module will
    -- only ever LEARN a skill named here, and will NEVER UNLEARN one that is -
    -- so the worst a bad `unlearn_skill` can do is nothing, and the worst a bad
    -- `learn_skill` can do is nothing. Ugga, whose herbalism and alchemy are
    -- both correct already, is protected by this without anybody remembering to
    -- protect her.
    --
    -- EMPTY MEANS "NO OPINION", AND THEREFORE "DO NOTHING". A character with no
    -- assignment is not a character that may be freely rearranged; it is one
    -- nobody has decided about, and the safe reading of an absent decision is
    -- to leave the character exactly as it is.
    ADD COLUMN `professions` VARCHAR(64) NOT NULL DEFAULT ''
        COMMENT 'Primary profession skill ids this character should end up holding, comma separated; empty = no opinion, touch nothing',

    -- THE STANDING ERRAND. One skill id, or 0 for none. It means: when this
    -- character is standing at a trainer who teaches this, learn it.
    --
    -- WHY IT IS ALSO WHAT AIMS THE WALK. `travel_npc` = 'profession trainer'
    -- resolves to the NEAREST spawn carrying UNIT_NPC_FLAG_TRAINER_PROFESSION,
    -- and in Elwynn that is as likely to be a cooking trainer as a tailor. An
    -- errand that lands a mage in front of a fishing instructor is travel that
    -- reports success and delivers nothing, which is this epic's signature
    -- failure. So when this column is set the module narrows the resolve to
    -- trainers that can actually teach THIS skill line.
    --
    -- CLEARED BY THE MODULE ON SUCCESS, and only on success. A failed attempt
    -- leaves it standing so the next poll retries, and the errand ends through
    -- the travel backstop rather than through a column that quietly forgot.
    ADD COLUMN `learn_skill` SMALLINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT 'Primary profession skill id to learn at the trainer; 0 = nothing to learn',

    -- THE DELIBERATE ACT. One skill id, or 0 for none. Unlearning destroys
    -- every point of a skill and every recipe hanging off it, and there is no
    -- undo, so it is never a side effect of anything: no code path anywhere
    -- unlearns to make room. It happens because this column says to.
    --
    -- WHY IT NEEDS NO TRAINER. Unlearning a profession is a spellbook action in
    -- 3.3.5, not a trainer one: the client sends CMSG_UNLEARN_SKILL and
    -- WorldSession::HandleUnlearnSkillOpcode does exactly one thing -
    -- `SetSkill(skillId, 0, 0, 0)` behind an IsPrimaryProfessionSkill guard
    -- (SkillHandler.cpp:91-100). The module does the same two things in the
    -- same order, so a character unlearning here is doing what a person at a
    -- keyboard would do, and nothing else.
    ADD COLUMN `unlearn_skill` SMALLINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT 'Primary profession skill id to give up; 0 = give up nothing',

    -- THE PRICE THE REQUESTER AGREED TO PAY, and the reason an unlearn cannot
    -- destroy work by accident.
    --
    -- The module refuses the unlearn when the LIVE skill value is above this
    -- number. So a request is not merely "drop alchemy", it is "drop alchemy,
    -- and I believe that costs at most N points". Og's alchemy is 1/75 and the
    -- bridge writes 1; if it had somehow reached 40 while the row sat in the
    -- queue, the request would be refused out loud instead of silently costing
    -- thirty-nine points nobody agreed to lose.
    --
    -- WHY A COST CHECK AND NOT A FLOOR. A fixed floor was tried in
    -- professions.py and removed, and the note there explains why: the family's
    -- own plan has Grug give up a herbalism he has worked to 41, so a floor
    -- would refuse the decision the family actually made. What must not happen
    -- is not "losing points" - it is "losing points NOBODY PRICED". This makes
    -- the price part of the request, which is a compare-and-swap on the cost:
    -- a requester whose picture of the world is stale gets a refusal, not a
    -- surprise.
    --
    -- 0 IS A REAL VALUE, not a sentinel: it means "only if this skill is worth
    -- nothing at all". The sentinel for "no request" lives in `unlearn_skill`,
    -- which is the column the module tests, so this one never has to carry two
    -- meanings at once.
    ADD COLUMN `unlearn_max` SMALLINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT 'Refuse the unlearn if the live skill value exceeds this; the cost the requester agreed to destroy';
