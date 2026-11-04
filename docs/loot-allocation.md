# AzerothCore Group Loot Allocation Analysis

**Commit**: 7f12e89ee5f467a50e62eba1d525eac7dc953d03 (mod-playerbots/azerothcore-wotlk fork, the `CORE_SHA` pinned in `.github/workflows/build.yml`)  
**Date**: 2026-11-03  
**Module**: mod-overseer

---

## 1. Source Files and Core Functions

### 1.1 Group.cpp — Primary Loot Logic Location

The loot allocation logic resides in `src/server/game/Groups/Group.cpp`, **not** in `LootMgr.cpp` as might be expected. The need eligibility check is `Player::CanRollForItemInLFG` in `src/server/game/Entities/Player/PlayerStorage.cpp:2469`.

| Function | Line | Purpose |
|----------|------|---------|
| `Group::GroupLoot` | 1122 | GROUP_LOOT method — creates rolls, sends `SMSG_LOOT_START_ROLL` to eligible members |
| `Group::NeedBeforeGreed` | 1287 | NEED_BEFORE_GREED method — passes `CanRollForItemInLFG() == EQUIP_ERR_OK` as each member's can-need flag (lines 1351, 1421) |
| `Group::MasterLoot` | 1442 | MASTER_LOOT method — sends `SMSG_LOOT_MASTER_LIST` to master looter |
| `Group::CountRollVote` | 1496 | Processes Need/Greed/Pass/Disenchant votes from clients |
| `Group::EndRoll` | 1547 | Handles roll timer expiry — auto-passes remaining votes |
| `Group::CountTheRoll` | 1603 | **Core resolution** — Need rolls first (random 1-100), then Greed/Disenchant |

### 1.2 CountTheRoll Detail (Lines 1603–1815)

This is the single function that decides the winner:

```cpp
// Need rolls win first
for (auto const& vote : roll->votes)
    if (vote.vote == NEED)
        // highest roll 1-100 wins; a tie keeps the earlier voter (strict <)

// If no Need votes, Greed rolls
for (auto const& vote : roll->votes)
    if (vote.vote == GREED || vote.vote == DISENCHANT)
        // highest roll 1-100 wins; a tie keeps the earlier voter (strict <)

// If all pass (lines 1806-1813): SendLootAllPassed, then item->is_blocked = false,
// so the item stays on the corpse and any group member may loot it
```

**Key behaviors**:
- Need beats Greed always (regardless of roll value)
- Roll is 1-100 random per voter
- Tiebreaker: `maxresul < randomN` is strict, so on a tie the voter met first in the roll's vote map keeps the item
- Master Loot bypasses all rolls — master looter assigns manually via `SMSG_LOOT_MASTER_LIST`
- All pass, under either method: the item is unblocked on the corpse for anyone in the group to loot. Round-robin (`m_looterGuid`, `Group::UpdateLooterGuid` at line 2080) only decides who may loot items **below** the loot threshold; `Group::GroupLoot` starts a roll only for items at or above it (line 1140)

---

## 2. Script Hooks Available to Modules

### 2.1 PlayerScript Hooks (PlayerScript.h)

| Hook Enum | Method | Parameters | Timing | Can Influence Winner? |
|-----------|--------|------------|--------|----------------------|
| `PLAYERHOOK_ON_GROUP_ROLL_REWARD_ITEM` | `OnPlayerGroupRollRewardItem` | `Player* player, Item* item, uint32 count, RollVote voteType, Roll* roll` | **After winner chosen, item created** | ❌ Observe only |
| `PLAYERHOOK_ON_LOOT_ITEM` | `OnPlayerLootItem` | `Player* player, Item* item, uint32 count, ObjectGuid lootguid` | After any loot pickup | ❌ Observe only |
| `PLAYERHOOK_ON_BEFORE_SEND_LOOT` | `OnPlayerBeforeSendLoot` | `Player* player, ObjectGuid lootGuid, Loot* loot` | Before loot window sent | ❌ Observe only |
| `PLAYERHOOK_ON_BEFORE_LOOT_MONEY` | `OnPlayerBeforeLootMoney` | `Player* player, Loot* loot` | Before money looted | ❌ Observe only |

**Critical finding**: `OnPlayerGroupRollRewardItem` fires **after** `CountTheRoll` has already selected the winner and created the item. The module receives the winner (`player`), the item, vote type (Need/Greed), and the `Roll*` object — but cannot change the outcome.

### 2.2 GroupScript Hooks (GroupScript.h)

| Hook Enum | Method | Parameters | Timing | Can Influence Winner? |
|-----------|--------|------------|--------|----------------------|
| `GROUPHOOK_ON_ADD_MEMBER` | `OnAddMember` | `Group*, ObjectGuid` | Member joins | ❌ |
| `GROUPHOOK_ON_REMOVE_MEMBER` | `OnRemoveMember` | `Group*, ObjectGuid, RemoveMethod, ObjectGuid, char const*` | Member leaves | ❌ |
| `GROUPHOOK_ON_CHANGE_LEADER` | `OnChangeLeader` | `Group*, ObjectGuid newLeader, ObjectGuid oldLeader` | Leader changes | ❌ |
| `GROUPHOOK_ON_DISBAND` | `OnDisband` | `Group*` | Group disbands | ❌ |
| `GROUPHOOK_ON_CREATE` | `OnCreate` | `Group*, Player* leader` | Group created | ❌ |
| `GROUPHOOK_CAN_GROUP_JOIN_BATTLEGROUND_QUEUE` | `CanGroupJoinBattlegroundQueue` | `Group const*, Player*, Battleground const*, ...` | Queue attempt | ❌ |

**No hooks** fire during roll creation, vote counting, or winner selection in Group.cpp.

### 2.3 LootScript Hooks (LootScript.h)

| Hook Enum | Method | Parameters | Timing | Can Influence Winner? |
|-----------|--------|------------|--------|----------------------|
| `LOOTHOOK_ON_LOOT_MONEY` | `OnLootMoney` | `Player* player, uint32 gold` | Money looted | ❌ Observe only |

**No hooks** for item loot allocation or roll resolution.

### 2.4 ItemScript Hooks (ItemScript.h)

All hooks are per-item and fire on use/quest/expire — **none** fire during group roll resolution.

---

## 3. mod-overseer Current Implementation

### 3.1 Hook Registration (mod_overseer.cpp)

The module overrides `OnPlayerGroupRollRewardItem` in its `PlayerScript` (`src/mod_overseer.cpp:7936` on main at f5a8acc). Abridged from that function:

```cpp
void OnPlayerGroupRollRewardItem(Player* player, Item* item, uint32 count,
                                 RollVote voteType, Roll* roll) override
{
    OverseerDecisions::LootedItem noted;
    Item* live = TakeLootedItem(player, item, count, noted);
    NoteGuildRunLoot(player, count, NotableLootEntry(live));
    {
        Loot* won = roll ? roll->getLoot() : nullptr;
        RecordLootWon(player, noted, count,
                      voteType == NEED ? OverseerDecisions::ItemVia::Need
                                       : OverseerDecisions::ItemVia::Greed,
                      won ? won->sourceWorldObjectGUID : ObjectGuid::Empty);
    }
    // ... loot council bookkeeping, then RecordItemLoot for the live item
}
```

### 3.2 Loot Tracking Structures (overseer_decisions.h)

```cpp
struct LootedItem
{
    std::uint32_t storyGuid{0};
    std::uint32_t guid{0};
    std::uint32_t entry{0};
};
```

`ItemVia` is a namespace of word constants (`Need` is `"need"`, `Greed` is `"greed"`, `Council` is `"council"`), not an enum.

### 3.3 Compatibility (overseer_core_compat.h)

`LootRollDisenchant(config)` reads the mod-playerbots config field under either spelling (`lootRollDisenchant` or `LootRollDisenchant`), the same `if constexpr (requires ...)` pattern the header uses for every renamed field. It is not a vote enum; `RollVote` comes from the core.

---

## 4. Three Allocation Options Compared

### Option A: Need Before Greed by Role Fit (Current AzerothCore LFG Behavior)

**How it works**: `Group::NeedBeforeGreed` (line 1287) and the NEED_BEFORE_GREED branch of the roll-start send (line 984) ask `Player::CanRollForItemInLFG()` (PlayerStorage.cpp:2469) whether each member may roll Need. That function returns OK at once unless the group is an LFG group and the looted object is inside its LFG dungeon map. Inside one, it checks, in order:
- `ITEM_FLAG2_EVERYONE_CAN_ROLL_NEED` lets anyone need
- the item's class and race masks, required spell and required skill
- a weapon needs its weapon skill; shields, librams, idols, totems and sigils need their class
- armor (not cloaks) must not be of a higher subclass than the class's best armor type: plate for paladin, death knight and warrior, mail for hunter and shaman, leather for druid and rogue, cloth otherwise; a warrior or rogue is refused any armor that carries spell power; with `CONFIG_LOOT_NEED_BEFORE_GREED_ILVL_RESTRICTION` set, armor above that item level must also be exactly that type

There is no tank/healer/dps role or primary-stat test, beyond the spell-power exclusion for warriors and rogues.

**Pros**:
- Already implemented in core (LFG dungeon finder uses this)
- Prevents "ninja looting" — warriors can't need on cloth, mages can't need on plate
- No configuration required

**Cons**:
- Only bites in an LFG group inside its LFG dungeon: a hand-made bot group on Need Before Greed gets `EQUIP_ERR_OK` for every member, so the check changes nothing for it
- Not applied under GroupLoot or MasterLoot
- Armor-type and proficiency only: no role or off-spec notion
- Cannot be extended by modules — no hooks in the eligibility check
- Rigid: a ret paladin needing on healing gear is blocked

### Option B: Rotation / Round-Robin

**How it works**: Track last winner per group, next eligible item goes to next member. The core has no rotation for rolled items: round-robin (`Group::UpdateLooterGuid`, line 2080) only picks who may loot items below the loot threshold, and an all-pass roll unblocks the item for everyone (CountTheRoll, lines 1806-1813). A module would build the rotation itself.

**Pros**:
- Simple, fair over long sessions
- Zero config
- Works with any loot method

**Cons**:
- Ignores item utility — a healer gets DPS trinket because "their turn"
- No concept of need vs want
- Hard to implement for modules — no hook at roll creation time to inject priority
- Core round-robin covers only below-threshold items; nothing lets a module intercept the Need/Greed phase

### Option C: EPGP-Style (Effort Points / Gear Points)

**How it works**: 
- EP = attendance, boss kills, time in raid (earned)
- GP = gear received (spent, weighted by slot/ilvl)
- Priority = EP / GP (higher = more deserving)
- On roll: eligible members sorted by PR, highest PR wins

**Pros**:
- Proven in high-end guilds for 15+ years
- Rewards attendance/consistency
- Naturally balances gear distribution over time
- Configurable weights per slot/quality

**Cons**:
- Requires persistent storage (EP/GP per character)
- Requires officer/maintainer to award EP (boss kills, attendance)
- No core support — must be fully implemented in module
- **No hook to inject priority at roll time** — would need to intercept before `CountTheRoll`

---

## 5. Recommendation

### Recommended: Option A (Need Before Greed by Role Fit) — **with core modification**

**Reasoning**:

1. **Already exists in core** — `Player::CanRollForItemInLFG()` (PlayerStorage.cpp:2469) implements an armor-type and proficiency check. It is gated to LFG groups inside LFG dungeons and read only under NEED_BEFORE_GREED, so a bot group needs both gates widened.

2. **Minimal module changes** — mod-overseer already tracks `ItemVia::Need` vs `ItemVia::Greed` in `OnPlayerGroupRollRewardItem`. The module can already *observe* the outcome; it just can't *influence* it.

3. **Solves the real problem** — The primary complaint in bot groups is "wrong armor type rolls need" (e.g., hunter needing on plate). The check refuses exactly that case. No measurement yet says what share of bad Need rolls it would remove.

4. **Extensible path** — If a hook were added at `Group::GroupLoot` roll creation (before `SMSG_LOOT_START_ROLL`), a module could:
   - Filter eligible Need voters by role fit
   - Inject custom priority (EPGP, rotation, etc.)
   - Still fall back to core random roll for tiebreak

5. **No persistent state required** — Unlike EPGP, role-fit is computed from live character data (class, spec, equipped items).

### What Would Need to Change in AzerothCore

A new hook in `Group::GroupLoot` (between the roll's creation at line 1143 and `SendLootStartRoll` at line 1198). This is an illustrative sketch of a hook that does not exist yet. A real one follows the core's pattern: a `GROUPHOOK_*` value in the enum in `src/server/game/Scripting/ScriptDefines/GroupScript.h`, a virtual on `GroupScript`, and a `ScriptMgr` dispatcher called from Group.cpp.

```cpp
// Sketch only: GroupScript has no such hook at 7f12e89e
virtual void OnGroupLootRollCreated(Group* group, Loot* loot, Roll* roll); // may edit who can need
```

Or simpler: expose `CanRollForItemInLFG` as a virtual method modules can override.

### What mod-overseer Can Do Today (Without Core Changes)

- **Observe and log** all roll outcomes via `OnPlayerGroupRollRewardItem` (already done)
- **Detect anomalies** — e.g., "player rolled Need on wrong armor type" — and flag for review
- **Implement EPGP externally** — track EP/GP in module DB, but **cannot enforce** at roll time; only post-hoc reporting
- **Master Loot assist** — if group uses Master Loot, module can suggest assignment to master looter via chat

---

## 6. Validation

Run local decisions test suite:

```bash
bash scripts/check-locally.sh decisions
```

Must pass before any documentation merge.

---

## 7. References

- `src/server/game/Groups/Group.cpp` — lines 984, 1122, 1287, 1442, 1496, 1547, 1603, 1806, 2080
- `src/server/game/Entities/Player/PlayerStorage.cpp` — line 2469 `Player::CanRollForItemInLFG`
- `src/server/game/Scripting/ScriptDefines/PlayerScript.h` — `PLAYERHOOK_ON_GROUP_ROLL_REWARD_ITEM`
- `src/server/game/Scripting/ScriptDefines/GroupScript.h` — no roll hooks
- `src/server/game/Scripting/ScriptDefines/LootScript.h` — `LOOTHOOK_ON_LOOT_MONEY` only
- `src/mod_overseer.cpp` — line 7936 `OnPlayerGroupRollRewardItem` (main at f5a8acc)
- `src/overseer_decisions.h` — `LootedItem`, `ItemVia` constants
- `src/overseer_core_compat.h` — `LootRollDisenchant` config-field accessor