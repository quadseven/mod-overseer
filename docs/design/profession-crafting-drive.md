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

## Anvil, Hammer and reagent-gated Blacksmithing recipes (#524)

Every "worn" Blacksmithing recipe (Runed Copper Belt through Imperial Plate
Boots) needs a nearby Anvil and an equipped Blacksmith Hammer, and the two
Green Iron recipes also need Green Dye from a vendor. DriveCraft casts in place
and the core's `CheckCast` refuses them, so they stay out of the recipe table.
The three decisions below are what the follow-up work builds against.

### Anvil: Anvil-gated recipes are OUT OF SCOPE for DriveCraft until the Hammer swap lands

Facts, read from the world database: every Anvil is a `GAMEOBJECT_TYPE_SPELL_FOCUS`
(type 8) with `data1 = 10`, the same value `SpellInfo::RequiresSpellFocus`
carries. It is a real spawn, not a DBC concept. `BuildTravelIndex` indexes
creature spawns only, so walking to an Anvil needs a second index of
GameObject spawns filtered to that type and focus, with the existing same-map
nearest-candidate ranking on top. The guild-bank walk is not a precedent: it
walks to a banker creature and searches for the vault after arriving, and
Anvils have no paired creature.

Decision: the travel half alone unblocks no recipe, because every Anvil recipe
also needs the Hammer. So Anvil travel is not built on its own. Until both
halves land the planner must not hand DriveCraft an Anvil-gated spell. A spell
that slips through still fails closed: `SPELL_FAILED_REQUIRES_SPELL_FOCUS` is
logged and the errand stands, which is the existing behaviour.

### Hammer: EquippedItemClass is not broad enough, so a temporary swap is the design

Facts: the Blacksmith Hammer (item 5956) is a main-hand weapon of class 2,
subclass 14 (`ITEM_SUBCLASS_WEAPON_MISC`). The worn recipes' `EquippedItemClass`
is weapon with a subclass mask for that miscellaneous subclass, not "any
one-handed weapon", so a character's real weapon never satisfies it. Equipping
the Hammer necessarily displaces the weapon in hand.

Decision: swap in, craft, swap back. There is no precedent in the module: the
gear-upgrade drive swaps one way and permanently. The swap needs these rules
before any code is written:

- Persist what was equipped before the swap (a roster column, like every other
  standing errand) so a crash mid-swap and a worldserver restart cannot strand
  the original weapon. On the next poll a stored weapon with the Hammer still
  in hand means "swap back first", before any new craft.
- Trigger only under `MayCraftNow`: alive, out of combat, not moving, not in a
  run. If a character is pulled into combat while holding the Hammer, the swap
  back takes precedence over the craft errand on the next out-of-combat poll,
  and the combat strategies are not touched.
- Require bag space for the displaced weapon before swapping; with none, leave
  the weapon where it is and the errand standing. Never drop or sell an item to
  make room.
- Read the world back after each swap (`GetItemByPos`), not the call's return,
  as the module does everywhere else a command should change the world.
- Death, logout or a reassigned job while the Hammer is in hand takes the same
  swap-back path on the next login poll, since the stored weapon is the only
  state needed.

Not built in this change: it needs the adapter build to verify, and it unblocks
nothing without the Anvil half.

### Reagents: craft reagents stay out of scope for town-trip buying

Green Dye is the only vendor-bought craft reagent in the Blacksmithing
progression, and its only users (Green Iron Leggings and Bracers) are Anvil
and Hammer gated. Extending the `kind='buy'` errand (food and drink today,
with money and bag-space checks) for a reagent that no reachable recipe uses
would be dead code, so craft reagents stay out of scope for town-trip buying
until those two recipes can run. When they can, the extension reuses the same
affordability and bag-space checks and adds Green Dye as the first non-food
item. Reagent buying for v1 remains the family's own gathering, as above.

### Recipe table

The deferred Blacksmithing recipes are not added to `RECIPES` in this change.
The table lives in the Python planner, not in this repository, and each entry
needs the two-source check (wowhead.com/wotlk and classicdb.ch) of spell id,
item id and reagents once the Anvil and Hammer halves exist.

## Why nothing was implemented in this pass

This module's C++ half (`DriveCraft`) lives in the adapter, which needs a
full AzerothCore + mod-playerbots build to compile or verify - not possible
standalone on the box this was written on. Writing an unverified adapter PR
blind, the way #438 explicitly declined to claim a build it could not run,
is the wrong tradeoff for a brand-new drive with no existing test coverage to
lean on. This doc, and the follow-up issue, are the honest deliverable for
this pass.
