# Enchanting and disenchanting (job='craft', Enchanting skill 333)

## The gap, precisely

`DriveCraft` (#440, `mod_overseer.cpp`) casts one standing recipe spell
against the bot's own inventory, trusting the core's real `CheckCast`/`Spell`
path the same way `TrainOnArrival` already trusts `Trainer::TeachSpell`. It
was built and verified against exactly one recipe shape:
`SPELL_EFFECT_CREATE_ITEM`, no target needed beyond the caster itself.

Enchanting (skill 333, held by Og alongside Tailoring, stuck at skill value 1)
does not fit that shape, and this doc exists to say precisely why, against the
real AzerothCore source at the pinned SHA
(`AC_CORE_SHA=47960183bb03b83e8943eb2f0f39c16df9710c9d`,
`mod-playerbots/azerothcore-wotlk`), not against what the leveling guide
implies.

## What was actually checked, and what it shows

### The "recipe" spells are a different effect type, with a different target

`SpellEffects.cpp`'s effect-handler table (lines ~125-126, 228):

```
&Spell::EffectEnchantItemPerm,       // 53 SPELL_EFFECT_ENCHANT_ITEM
&Spell::EffectEnchantItemTmp,        // 54 SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY
&Spell::EffectEnchantItemPrismatic,  //156 SPELL_EFFECT_ENCHANT_ITEM_PRISMATIC
```

"Enchant Bracer: Minor Health" and every other permanent-enchant recipe in
the leveling guide (`docs`/scratchpad extract, `## Enchanting` section) casts
`SPELL_EFFECT_ENCHANT_ITEM` (53), handled by
`Spell::EffectEnchantItemPerm` (`SpellEffects.cpp:2858`). Read in full, that
handler:

- Requires `itemTarget` to be non-null and returns immediately if it is not.
- Reads the item's *owner* (`itemTarget->GetOwner()`), not the caster, and
  calls `item_owner->ApplyEnchantment(itemTarget, PERM_ENCHANTMENT_SLOT, ...)`
  - i.e. the effect literally modifies an item object the character already
    owns, in place. There is no item produced, nothing to bank or vendor.
- Only calls `p_caster->UpdateCraftSkill(m_spellInfo->Id)` (the skill-up
  roll) in the non-vellum branch - so the skill-up itself is conditioned on
  a real item target existing and going through this path, not a side effect
  of the cast alone.

Mechanically, this means the cast cannot be issued the way `DriveCraft` casts
`SPELL_EFFECT_CREATE_ITEM` recipes today:

```cpp
// DriveCraft, mod_overseer.cpp:10928 - works for CREATE_ITEM, not for this
SpellCastResult const result = bot->CastSpell(bot, spellId, false);
```

`Unit::CastSpell` (checked against every overload in `Unit.h:1675-1681`) has
**no overload that takes an `Item*` target.** The only path that reaches an
item target is the `SpellCastTargets const&` overload
(`Unit.h:1675`), built with `SpellCastTargets::SetItemTarget(Item*)` before
the cast - the same construction the real client performs when a player
drags a bracer into the enchant-target square. This is a different, and
larger, call shape than `DriveCraft`'s one-liner: it needs to (a) pick which
owned item to enchant (the bracer/cloak/shield/weapon the recipe names,
unenchanted or enchanted with something worse), (b) build `SpellCastTargets`
around it, and (c) use the targets-based cast overload instead of the
unit-target one `DriveCraft` was written against.

**Conclusion: enchanting is castable through the same `CheckCast`-trusting
philosophy `DriveCraft` already uses, but not through `DriveCraft`'s existing
function body.** It needs its own item-targeting cast path - a new action
kind, not a new `RECIPES` row.

### Disenchanting is a third, structurally different mechanic - not a recipe at all

`Strange/Soul/Vision/Dream/Illusion Dust` and the various Essences - the
reagents nearly every enchant recipe in the guide needs - are not gathered or
crafted. They come from `SPELL_EFFECT_DISENCHANT` (99),
`Spell::EffectDisEnchant` (`SpellEffects.cpp:4468`), reproduced here in full
because its shortness is the point:

```cpp
void Spell::EffectDisEnchant(SpellEffIndex /*effIndex*/)
{
    if (effectHandleMode != SPELL_EFFECT_HANDLE_HIT_TARGET)
        return;

    if (!itemTarget || !itemTarget->GetTemplate()->DisenchantID)
        return;

    if (Player* caster = m_caster->ToPlayer())
    {
        caster->UpdateCraftSkill(m_spellInfo->Id);
        caster->SendLoot(itemTarget->GetGUID(), LOOT_DISENCHANTING);
    }

    // item will be removed at disenchanting end
}
```

Two facts here that matter more than the spell-effect-ID trivia:

1. **It requires `ItemTemplate::DisenchantID` to be set** (confirmed present
   on `ItemTemplate` in `ItemTemplate.h:684,690` as `RequiredDisenchantSkill`
   / `DisenchantID`) - i.e. the disenchant target must itself be a real,
   currently-owned, disenchantable equipment item (a green/blue the family
   would otherwise vendor or auction), not a reagent bought or gathered. This
   is a second tradeskill action with its own item-target-casting
   requirement, same as enchanting above, but the item it consumes is a
   *different* character's/plan's asset, not a fungible material.
2. **The effect does not hand over a reagent. It opens a loot window**
   (`caster->SendLoot(itemTarget->GetGUID(), LOOT_DISENCHANTING)`) against a
   loot template keyed by `DisenchantID` (the `disenchant_loot_template`
   table in `acore_world`, cross-checked: this is a genuinely separate loot
   table from `item_loot_template`, rolled the same probabilistic way a
   creature's loot table is). The comment even says it plainly: "item will be
   removed at disenchanting end" - the *end*, not this call. A real client
   then sends the loot-window packets (`CMSG_AUTOSTORE_LOOT_ITEM` /
   `CMSG_LOOT_RELEASE`) to actually claim the rolled reagent and close the
   window. **A bot has no client to send those.**

This is the same "delivered is not done" shape this repo's own `AGENTS.md`
already warns about for `Trainer::TeachSpell` and the areatrigger/instance-exit
mechanics - a call that reports success to a client, which a bot does not
have - except here it is worse: `TeachSpell`'s effect is at least applied
immediately and can be verified after the fact with `HasSkill`. Disenchant's
effect (the reagent landing in the bag) **does not happen at all** until a
second, entirely separate action (server-side loot resolution) completes,
and nothing in `mod-overseer.cpp` or `mod-playerbots` (checked: `gh search
code` for `Disenchant`/`LootMgr`/`AutoStoreLoot`/`LootAction` across
`quadseven/mod-playerbots` returns zero hits) resolves a loot window without
a client on the other end.

### The reagent supply also collides with an existing, working plan

`disposition.py` (infra) already models `DISENCHANT` as one of the six
routes an item can take, and has already reasoned about exactly this gap.
Quoting its own `EXECUTABLE_TODAY` comment directly, because it says the
state of the world more precisely than a paraphrase would:

> `DISENCHANT` no executor, and `overseer_command.kind` is an ENUM of 18
> values that does not contain 'disenchant', so such a row cannot even be
> inserted. Og, the family enchanter, is skill 1 of 75, which would cover 38
> of the 155 carried greens.

So the reagent side of Enchanting is blocked on three things at once, none
of which this pass can safely do in isolation:

1. A DB migration to `overseer_command.kind`'s enum (schema change, its own
   review surface).
2. A C++ executor for that command, which per the finding above is not "cast
   a spell" but "cast a spell, then resolve a loot window with no client" -
   a genuinely new mechanism, not a copy of `DriveCraft` or the personal-bank
   deposit driver.
3. A decision in `disposition.py` about which greens go to disenchant instead
   of vendor - today `EXECUTABLE_TODAY = {KEEP, VENDOR, GIVE}` and vendor
   already ships and has written thousands of rows against exactly the same
   items disenchant would compete for. Turning disenchant on without an
   allocation rule means either starving Og's dust supply (vendor keeps
   winning) or cutting into the vendor gold pipeline the family currently
   relies on - a real trade-off requiring a decision, not a default.

## Design decision: two new action kinds, not an extension of `DriveCraft`

**`DriveCraft` should not be stretched to cover this.** Its whole design -
one `craft_spell` column, one unit-target cast, one "does CheckCast accept
this" boolean - is correct and should stay exactly that simple for the
`CREATE_ITEM` recipes it already covers (Tailoring, and eventually
Blacksmithing/Leatherworking/Engineering/Alchemy's item outputs). Bolting an
item-target cast path and a loot-resolution path onto it would turn one
readable function into three unrelated state machines sharing a name, which
is the same "second source of truth" mistake this codebase's own
`DriveCraft` comment explicitly avoided for `CheckCast` (`mod_overseer.cpp`
~line 10837).

Instead, this is honestly **two new drives**, both out of scope for this
pass:

- **`DriveEnchant`**: a new roster column (e.g. `enchant_spell` +
  `enchant_target_slot`, or an item GUID once one is picked), a new
  item-target cast path built on `SpellCastTargets`/the targets-taking
  `CastSpell` overload, applied against an owned, currently-unenchanted (or
  worse-enchanted) item in the named equipment slot. This part is smaller
  than disenchanting - no loot-window problem, `CheckCast` still does the
  "does this item already have a better enchant" refusal for us - but it is
  still a different call shape than `DriveCraft`'s existing body, verified
  above against `Unit.h`.
- **`DriveDisenchant`** (blocked on the `overseer_command.kind` migration and
  a `disposition.py` allocation rule): casts the disenchant spell against a
  chosen owned green/blue, **and** resolves the resulting `LOOT_DISENCHANTING`
  window server-side with no client - which needs its own verification pass
  against AzerothCore's loot-release/auto-store code path before anyone
  writes it, exactly the kind of "verify the exact 3.3.5a signature against
  a real core checkout before writing this" caution `mod-overseer#437`
  already flagged for the unrelated `Guild` API. This is the part of
  Enchanting that is genuinely novel work, not a re-shape of an existing
  drive.

## What a small, safe first slice would still need

The task that produced this doc asked specifically whether "just the early
Enchant Bracer: Minor Health loop" could be scoped narrowly. It cannot,
honestly, for a reason independent of the cast-shape work above: **the loop
has a hard prerequisite, not a soft one.** Enchant Bracer: Minor Health
consumes 48 Strange Dust; Strange Dust has exactly one source
(disenchanting); disenchanting has zero executors today. A `DriveEnchant`
built without `DriveDisenchant` first would sit exactly where `DriveCraft`
already sits for a reagent-starved character: correctly refusing every cast,
forever, for a family that owns no Strange Dust and has no way to make any.
Shipping the enchant half alone would be shipping a drive with a permanently
false precondition - observable, not a bug, but not a real first slice
either.

**The actual smallest real slice is `DriveDisenchant` first**, because it is
the one both halves depend on and it is where all three blockers above
(migration, loot-window mechanism, `disposition.py` allocation rule) live.
That is exactly the shape of engineering effort - schema change + a genuinely
unverified core interaction + a cross-cutting policy decision competing with
a shipping feature - this project's own precedent (`mod-overseer#436`, the
guild-bank design-only pass) treats as "design doc + follow-up issue," not
"implement this session."

## What this explicitly defers

- Both `DriveEnchant` and `DriveDisenchant` C++ implementation - filed as a
  follow-up issue, gated on someone with real build access, same shape
  `mod-overseer#440` already used for `DriveCraft` itself.
- The `overseer_command.kind` enum migration and its own review (a schema
  change belongs in its own PR, not bundled into a drive).
- The `disposition.py` disenchant-vs-vendor allocation rule - a real,
  measurable trade-off (dust supply vs. the vendor gold pipeline currently
  writing thousands of rows) that deserves its own numbers before a default
  is picked, not a guess made inside this doc.
- Rockbiter Weapon-style `SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY` recipes (the
  guide's leveling path is entirely permanent enchants; temporary enchants
  are a PvP/consumable use case outside this family's current needs) and
  `SPELL_EFFECT_ENCHANT_ITEM_PRISMATIC` (socketing) - out of scope, not
  reached by anything in the leveling guide.

## Why nothing was implemented in this pass

Same reasoning as `mod-overseer#436` and `#439` before it: the C++ half of
either new drive needs a full AzerothCore + mod-playerbots build to compile
or verify (not available standalone in this session), and `DriveDisenchant`
specifically needs verification of a mechanism - server-side loot-window
resolution with no client - that nothing in this codebase or
`mod-playerbots` has ever done, per the `gh search code` results above.
Writing either drive without that verification would be exactly the
"plausible-looking but unverified" failure mode this project's own history
warns against. This doc, and the follow-up issue, are the honest deliverable
for this pass.
