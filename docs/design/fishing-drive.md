# Fishing drive (job='fish')

## The gap, precisely

Five roster characters exist, none of them can raise Fishing, and nothing in
this module has ever tried to change that - `JobModes()` had no `"fish"`
entry before this change, and no drive read anything Fishing-shaped off
`overseer_roster`.

## What upstream mod-playerbots actually offers (checked first, on purpose)

Same discipline as the craft-leveling pass before this one: before designing
anything from scratch, does mod-playerbots already have a native "go fish"
bot action this module could just drive?

It has far more than that. `src/Ai/Base/Actions/FishingAction.cpp` (pinned
mod-playerbots SHA) is a **complete** fishing AI:

- `EquipFishingPoleAction` finds a fishing pole in the bot's own bags and
  equips it, or - for a random bot only - tries to buy one out of thin air
  (`sRandomPlayerbotMgr.IsRandomBot(bot)` gate; irrelevant to this roster).
- `MoveNearWaterAction` searches a radius around the bot (or around its
  master, if one is following) for fishable water via `FindWaterRadial`, or
  for a `GAMEOBJECT_TYPE_FISHINGHOLE` game object, and walks to a spot with
  line of sight to it.
- `FishingAction` orients the bot at the water and casts `FISHING_SPELL`
  (spell id 7620, "Fishing") against itself.
- `CanUseFishingBobberValue`/`UseBobberAction` watch for the resulting
  `FISHING_BOBBER` (game object entry 35591) to reach `GO_READY` - the moment
  a real client would show a splash - and call `GameObject::Use` on it, which
  is the core's own catch-resolution path (loot table roll, skill-up check,
  bank-full handling, all of it - nothing about the catch itself is
  reimplemented anywhere in mod-playerbots or in this module).
- `EndMasterFishingAction` turns the whole loop off once there is no more
  fishable water within reach.

All of this sits behind exactly one strategy: `MasterFishingStrategy`,
registered as `"master fishing"` (`NonCombatStrategy.cpp`,
`StrategyContext.h`). Upstream turns it on in exactly one place -
`SeeSpellAction.cpp`, when a bot **sees its own real, logged-in master cast
Fishing nearby** - and off in exactly one other (`FishingAction.cpp`'s own
`EndMasterFishingAction`, plus the `follow`-strategy check inside
`FollowActions.cpp` that stops a bot leaving a fishing master behind while
this strategy is active).

**This is not the right primitive to build on top of. It is the answer.**
Nobody attended is a real player master here; the fix is not to build a
fishing action, it is to turn the strategy on directly, by job, the same
"thin driver over the bot's own AI" shape `DriveQuests` already is for
upstream's quest AI. See `git log` on this file for the (much larger) design
pass that reached the identical conclusion for `SetCraftAction` and rejected
it, and contrast: that one really was the wrong primitive and needed a
replacement (`DriveCraft`'s direct `CastSpell`). This one did not need
replacing at all.

## The one gap upstream leaves: getting the skill

`CanFishValue::Calculate` (`FishValues.cpp`) refuses outright when
`bot->GetSkillValue(SKILL_FISHING)` is 0 - there is no path from "strategy
on" to "bot fishes" without the skill already present. The upstream code that
would otherwise grant secondary skills automatically -
`PlayerbotFactory::InitTradeSkills` - opens with:

```cpp
if (!sRandomPlayerbotMgr.IsRandomBot(bot))
    return;
```

Every character on this roster runs on a named account and is therefore
never a random bot - the exact gate this project's own `AGENTS.md` already
lists as "the first thing to suspect" for professions, talents, bag grants,
trainer spells, the dungeon finder and the dead-bot rescue. Fishing joins
that list. So this family starts holding no Fishing skill at all, and it has
to come from a real Fishing trainer, the same "no magic" discipline every
other skill in this file already holds to.

## What shipped in this pass

**`overseer_roster.learn_fishing`** (TINYINT(1), default 0) -
`2026_09_12_02_overseer_roster_fishing.sql`. Set to 1 to ask this character
to learn Fishing from the next trainer it happens to reach that offers it.
Cleared by the module once `HasSkill(SKILL_FISHING)` reads back true - never
assumed from `Trainer::TeachSpell`'s own void return, for the identical
"delivered is not worked" reason `TrainOnArrival` already reads back
`character_skills` rather than trusting the purchase call.

**`TrainFishingOnArrival(name, bot, entry)`** (`mod_overseer.cpp`) - hooked at
the exact same opportunistic call site as the existing
`DiscoverFlightPointOnArrival`, right after it, inside the "arrived at an
aimed creature" branch of the travel-errand loop. It does **not** hook into
`ProfessionPlan`'s aim/plan-matching machinery at all: `learn_skill` and its
trainer path are built entirely around a primary-profession slot (max two per
character), a declared end state (`professions`), and a give-up path priced
by `unlearn_max` - none of which describes Fishing, a secondary skill with no
cap and nothing to give up. `SkillStartedBySpell`, the helper `learn_skill`'s
own resolver depends on, explicitly excludes Fishing via
`IsPrimaryProfessionSkill` and says so in its own comment. Bending that
struct to also mean "one specific secondary skill, no slot, no cap" would
make it lie about the thing its name says for every OTHER caller. A sibling
pair - `SpellTeachesSkill`/`TrainerSpellForFishing`, same shape as
`SkillStartedBySpell`/`TrainerSpellForSkill` with the primary-profession
filter removed - is a few lines and keeps both meanings honest.

Being opportunistic (checked on *every* arrival at *any* aimed creature, a
no-op unless that creature happens to be a trainer that teaches Fishing) means
this needs no travel-aim of its own: whatever sends a character to a trainer
for any other reason (`travel_npc='trainer'`, `='profession trainer'`, a
class trainer visit) is also the delivery mechanism for this. Python's whole
job is deciding **when** to point a character at a trainer and setting
`learn_fishing=1` first; it does not need a new travel keyword.

**`DriveFish()`** (`mod_overseer.cpp`) - reads `job` off every roster row
(same `LoadJobs()` every other job-mode drive reads) and, for each
`job='fish'` character, toggles `"master fishing"` via
`PlayerbotAI::ChangeStrategy` based on `OverseerDecisions::NextFishDriveStep`
(`overseer_decisions.h`/`.cpp`, unit-tested in `tests/test_fish_drive.cpp`
with no world, no bot, and no database - the same pure-decision pattern
`NextProfessionStep` already established). The decision is deliberately
small: turn on only once the skill is confirmed present (never uselessly
early - `isUseful()` on every action underneath the strategy would just poll
false forever), turn off the moment `job` moves away, and do nothing on every
other poll, which is the steady state a standing errand spends nearly all of
its life in.

**Nothing about the fishing loop itself was written.** `MoveNearWaterAction`,
`FishingAction` and `UseBobberAction` already find water, cast, and land the
catch entirely on their own once the strategy is on and the skill is
present - there was no gap left to fill there.

## What this defers, and why it is safe to defer

Apprentice Fishing (skill 1-75) is the whole of what ships here: "learn from
any Fishing trainer, then cast at whatever water mod-playerbots' own
`FindWaterRadial` finds within its default search window" needs nothing past
what is already built. Everything past Apprentice is a real, separate scope:

- **Journeyman (75) and Expert (150)** both gate on visiting a trainer again
  once the skill value crosses the current cap - mechanically identical to
  what `TrainFishingOnArrival` already does (it re-checks
  `TrainerSpellForFishing` every time, which already returns the next tier
  once `CanTeachSpell`'s own rank-chain check says the character qualifies),
  so this is very likely **already covered** by what shipped here. It has
  not been proven live against a real trainer visit past Apprentice in this
  pass, which is the honest reason it is called out rather than claimed.
- **Artisan (225) requires buying a specific book from a specific vendor**
  (not a trainer purchase at all - an item that teaches a spell on use), which
  `Trainer::TeachSpell` cannot reach. This needs its own small mechanism:
  locate the vendor, confirm the bot can afford and carry the item, buy it,
  use it, read back `GetPureMaxSkillValue(SKILL_FISHING)` the same way
  `TrainOnArrival` already does for a tier-up. Genuinely new code, not a
  variant of anything above.
- **Master/"Nat Pagle, Angler Extreme" (300, historically the Artisan-tier
  quest in this content era) is quest-driven**, not a purchase at all - it
  needs `DriveQuests`' own machinery (quest acceptance, objective tracking,
  turn-in) pointed at a specific quest chain, which is a different drive
  entirely and well outside what a fishing-specific change should carry.

**Follow-up**: file a `quadseven/mod-overseer` issue scoping the
Journeyman/Expert verification pass and the Artisan vendor-book mechanism
separately, referencing infra#2757 (the craft-leveling epic this is a
sibling of, not a part of) as related context. Do not block shipping
Apprentice on either.

## The Python side (design only - not implemented in this pass)

**Why design-only.** This module's Python counterpart
(`production/scripts/wow-overseer/`) lives in `quadseven/infra`, a repository
other agents were actively working in concurrently while this pass ran. The
standing-errand mechanics below reuse patterns already proven in that
codebase (`professions.py`'s `_write_trade_errand`), so this is not a
from-scratch design - it is naming the same shape for a new column, sized so
whoever picks it up in `infra` does not have to re-derive it.

1. **`fishing.py`** (new, sibling to `professions.py`): decides which
   characters should be assigned `job='fish'` right now. A simple first cut -
   idle time, or a standing low-priority errand behind quest/craft/gather
   jobs - is enough; this is a scheduling decision, not a skill-progression
   one, since the skill-up itself is entirely the C++ side's job once the
   strategy is on.
2. **Bootstrapping the skill, once per character**: write `learn_fishing = 1`
   for any character with `job` about to become `'fish'` and
   `GetSkillValue(SKILL_FISHING) == 0` (readable the same way `professions.py`
   already reads `character_skills` for `professions.settled`), then send it
   toward any trainer that offers `UNIT_NPC_FLAG_TRAINER` - reusing
   `travel_npc='trainer'`, the widest existing keyword, is enough, because
   `TrainFishingOnArrival` is a no-op for any trainer that does not happen to
   teach Fishing and simply waits for a poll that lands on one that does. A
   tighter `'fishing trainer'` `travel_npc` keyword (mirroring
   `'profession trainer'`'s own UNIT_NPC_FLAG resolution) is a worthwhile
   follow-up once this is live, so a character is not left to wander several
   trainers before one happens to teach Fishing - but it is an optimization,
   not a correctness requirement, exactly the same relationship
   `'profession trainer'` has to plain `'trainer'` today.
3. **No geometry lookup needed.** This is the headline simplification this
   design pass found: `map_server.py`'s `Geometry`/`GEO` helpers do not need
   to be touched, because `MoveNearWaterAction`'s own `FindWaterRadial`
   already answers "is there water near me" using the live server's own
   liquid data (`Map::GetLiquidData`) - the exact geometry a Python-side
   lookup would otherwise have to approximate from static zone data, and
   worse, because Python has no view of runtime phasing or instance state at
   all. Python's only responsibility is **which zone** a character with
   `job='fish'` is standing in when the job is assigned - any starting zone
   with a coastline or a river works for Apprentice - not where the water is
   within it.
4. **Setting `job='fish'`** reuses the existing `jobs.resolve`/`DoJob`
   command path unchanged, once one addition lands there: `jobs.py`'s own
   mirror of `JobModes()` (duplicated on purpose, per this module's existing
   `tests/test_job_mode.py` convention for `TravelRoles`/`JobModes`) needs
   `"fish"` added, or `jobs.resolve` will refuse the mode before it ever
   reaches this module. **This is a required companion change in `infra`,
   not an optional one** - without it, `DoJob` on the C++ side never sees
   `"fish"` at all, because the string is validated and rejected on the
   Python side first.
