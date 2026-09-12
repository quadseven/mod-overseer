# Profession crafting/leveling drive (job='craft')

## The gap, precisely

`professions.py` and `bridge.py`'s trade-errand machinery already answer "how
does a character LEARN a trade" (walk to a trainer, buy the skill through
`TrainOnArrival`, infra#2757). That is solved and working.

What is not solved: once learned, GATHERING trades (mining, herbalism,
skinning) level up on their own as an incidental side effect of normal
`job='quest'` play - ore and herb nodes get picked up in passing, skinning
happens on kill-loot. Verified live against the family's real
`character_skills` this session: Ugga's Herbalism sits at 132/225 with no
dedicated drive ever built for it, and Bork's Skinning climbed from 0 to
12/75 within about twenty minutes of ordinary questing.

CRAFTING/production trades (blacksmithing, leatherworking, tailoring,
engineering, alchemy, enchanting) do not level at all. Every family member who
holds one is stuck at skill value 1. `mod_overseer.cpp`'s `JobModes()` lists
`"craft"` as a syntactically valid job value, and its own comment says
plainly: only `'quest'` has a real drive behind it. Every other mode -
`"craft"` included - just stands the quest drive down and does nothing.
`craftpleas.py`'s own docstring says the same thing from a different angle:
"mod-overseer has no verb that turns a tradeskill into an item."

## What upstream mod-playerbots actually offers (checked first, on purpose)

Before designing anything from scratch: does mod-playerbots already have a
native "craft an item" bot action mod-overseer could just drive, the same way
`DriveQuests` is a thin driver over the bot's own quest AI rather than
mod-overseer reimplementing questing?

It has exactly one relevant pair of files -
`src/Ai/Base/Actions/SetCraftAction.cpp` and `src/Ai/Base/Value/CraftValue.h`
- and reading them closes this question rather than opening it further: **this
is not the right primitive.** `SetCraftAction` is the handler for a chat
command (`craft <itemlink>` / `craft reset` / `craft ?`) that a REAL, LOGGED-IN
master issues to a bot they are actively playing with. It inspects the bot's
own learned spells for one that creates the named item, records the reagents
that recipe needs into `CraftData`, and then waits - `CraftData::AddObtained`
is fed by the bot's own trade-window handling as the master hands reagents
across in a live trade, and only once `IsFulfilled()` would something (not in
either of these two files - not found anywhere else in the tree either)
actually cast the spell. It is built for "a person is at the keyboard trading
mats to their alt," which describes nobody in this family: five server-side
bots with an attended client only occasionally, never trading each other
across a live trade window on a schedule.

Trying to drive `SetCraftAction` from mod-overseer would mean simulating a
live master trade session per craft attempt - strictly harder than the
alternative below, for no benefit.

**The primitive that actually fits**: a tradeskill recipe in WoW is just a
spell with `SPELL_EFFECT_CREATE_ITEM`, whose reagents are declared on the
`SpellInfo` itself (`Effects[i].ItemType`, `Reagent[x]`/`ReagentCount[x]` -
literally the same fields `SetCraftAction::Execute` reads to build `CraftData`
in the first place). A bot that already knows the recipe spell and already
carries the reagents in its own bags needs nothing from the trade system at
all - a direct spell cast against itself (the same shape `TrainOnArrival`
already uses for the trainer purchase, via `Trainer::TeachSpell`) lets the
same core code that runs when a real player clicks "Create" in their
tradeskill window resolve reagent consumption and the skill-up roll, with
mod-overseer only deciding WHEN and WHICH spell - the same shape as every
other drive in this file.

## Design for `job='craft'`

**Python side** (new module, sibling to `professions.py`, or an extension of
it - TBD by whoever picks this up, but should reuse `professions.SKILL_IDS`
and `professions.CRAFTING` rather than re-deriving them):

1. For a character whose `professions.assigned(name)` includes a crafting
   trade currently below its bracket cap, pick ONE cheap, always-craftable
   recipe at the character's current skill level - the standard WoW
   private-server "vendor mats, trivial recipe, spam it" pattern (e.g.
   Rough Sharpening Stone for early Blacksmithing, Linen Bandage for early
   First Aid-adjacent trades, etc. - concrete recipe IDs need a real pass
   against `acore_world.item_template`/`skill_line_ability`, the same tables
   `guildcraft.py` already knows how to query for "what does this trainer
   teach and what does it need").
2. Write a standing craft errand, same shape as the trainer errand: a new
   pair of roster columns (`craft_spell` and maybe `craft_count`, or reuse
   `learn_skill`'s column-per-verb pattern) so a worldserver restart cannot
   lose it, re-asserted idempotently every planning cycle exactly like
   `_write_trade_errand` does today.
3. Materials: **v1 assumes the character already holds enough reagents from
   its own gathering** (mirrors `_holders`/`_keepers` in `professions.py`,
   which already reasons about "does a gatherer's output feed a craft
   somebody in the family owns"). Buying mats from a vendor or the AH is
   explicitly out of scope for v1 - flag it as the next increment, not a
   blocker for this one.
4. Rank-up trainer visits (Apprentice -> Journeyman -> Expert -> Artisan ->
   Master) reuse the EXISTING `learn_skill`/`travel_npc='profession trainer'`
   mechanism unchanged - the Python planner just re-emits that errand again
   once skill value crosses the current rank's cap, exactly the same call it
   already makes for the first-time learn.

**C++ side** (`mod_overseer.cpp`, new `DriveCraft`, parallel to
`DriveQuests`/`TrainOnArrival`):

1. Runs every `job='craft'` character each `OnUpdate`, same cadence as the
   other drives.
2. Reads the standing craft errand off the roster row.
3. Verifies (a) the bot knows the spell, (b) the bot's own inventory holds
   enough of every declared reagent - `SetCraftAction`'s reagent-lookup loop
   over `SkillLineAbilityEntry`/`SpellInfo::Effects`/`Reagent[]` is directly
   reusable READ-ONLY logic even though the action itself is not (there is no
   reason to duplicate that table walk by hand).
4. Casts the spell directly against the bot itself, exactly the "no magic"
   bar `TrainOnArrival` was already held to - the core's own spell-cast path
   does the reagent consumption and the skill-up roll; this module does not
   touch `character_skills` or the bot's bags itself.
5. Clears/re-derives the standing errand once the bracket cap is hit (which
   then surfaces as "go see the trainer again" via the existing mechanism in
   step 4 above, Python side).

## What this explicitly defers

- **Material acquisition** (buying reagents from a vendor or the AH when the
  family's own gathering hasn't produced enough). v1 refuses rather than
  invents mats out of nowhere - same "no magic" discipline as everything else
  in this epic.
- **Recipe selection quality** beyond "cheapest known recipe that is
  currently trainable" - no attempt at optimal skill-up routing (the kind of
  spreadsheet a min-maxing human would build) for v1.
- **Enchanting and Alchemy's non-item outputs** (an enchant applies to gear,
  a flask/potion is consumed rather than banked) - the craft-and-hold model
  above assumes a CREATE_ITEM spell that leaves a bankable/sellable item
  behind, which covers the majority of early recipes but not all of them;
  worth a second look once v1 lands for the trades that are the exception.

## Why nothing was implemented in this pass

This module's C++ half (`DriveCraft`) lives in the adapter, which needs a
full AzerothCore + mod-playerbots build to compile or verify - not possible
standalone on the box this was written on. Writing an unverified adapter PR
blind, the way #438 explicitly declined to claim a build it could not run,
is the wrong tradeoff for a brand-new drive with no existing test coverage to
lean on. This doc, and the follow-up issue, are the honest deliverable for
this pass.
