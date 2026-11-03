# AzerothCore Group Loot Allocation Analysis

**Commit**: 8a4e76eac8c4ccebf547113afe7b51d6c3e91641 (mod-playerbots/azerothcore-wotlk fork)  
**Date**: 2026-11-03  
**Module**: mod-overseer

---

## 1. Source Files and Core Functions

### 1.1 Group.cpp — Primary Loot Logic Location

The loot allocation logic resides in `src/server/game/Entities/Player/Group.cpp`, **not** in `LootMgr.cpp` as might be expected.

| Function | Line | Purpose |
|----------|------|---------|
| `Group::GroupLoot` | 1123 | GROUP_LOOT method — creates rolls, sends `SMSG_LOOT_START_ROLL` to eligible members |
| `Group::NeedBeforeGreed` | 1288 | NEED_BEFORE_GREED method (LFG) — checks `CanRollForItemInLFG()` for role-based eligibility |
| `Group::MasterLoot` | 1443 | MASTER_LOOT method — sends `SMSG_LOOT_MASTER_LIST` to master looter |
| `Group::CountRollVote` | 1497 | Processes Need/Greed/Pass/Disenchant votes from clients |
| `Group::EndRoll` | 1548 | Handles roll timer expiry — auto-passes remaining votes |
| `Group::CountTheRoll` | 1604 | **Core resolution** — Need rolls first (random 1-100), then Greed/Disenchant |

### 1.2 CountTheRoll Detail (Lines 1604–1750)

This is the single function that decides the winner:

```cpp
// Need rolls win first
for (auto const& vote : roll->votes)
    if (vote.vote == NEED)
        // highest roll 1-100 wins tie goes to first

// If no Need votes, Greed rolls
for (auto const& vote : roll->votes)
    if (vote.vote == GREED || vote.vote == DISENCHANT)
        // highest roll 1-100 wins tie goes to first

// If all Pass, item goes to round-robin (GroupLoot) or remains on corpse (NeedBeforeGreed)
```

**Key behaviors**:
- Need beats Greed always (regardless of roll value)
- Roll is 1-100 random per voter
- Tiebreaker: first voter to roll that value wins
- Master Loot bypasses all rolls — master looter assigns manually via `SMSG_LOOT_MASTER_LIST`
- Group Loot uses round-robin for unrolled items; Need Before Greed leaves on corpse

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

The module registers `OnPlayerGroupRollRewardItem` via `PlayerScript`:

```cpp
// In OverseerPlayerScript class
void OnPlayerGroupRollRewardItem(Player* player, Item* item, uint32 count,
                                 RollVote voteType, Roll* roll) override
{
    // Records the loot for tracking / guild runs / loot council
    OverseerDecisions::LootedItem noted;
    Item* live = TakeLootedItem(player, item, count, noted);
    NoteGuildRunLoot(player, count, NotableLootEntry(live));
    RecordLootWon(player, noted, count,
                  voteType == NEED ? OverseerDecisions::ItemVia::Need
                                   : OverseerDecisions::ItemVia::Greed,
                  won ? won->sourceWorldObjectGUID : ObjectGuid::Empty);
    // ... council tracking
}
```

### 3.2 Loot Tracking Structures (overseer_decisions.h)

```cpp
struct LootedItem {
    std::string name;
    uint32 entry = 0;
    uint32 count = 0;
    ItemQuality quality = ITEM_QUALITY_POOR;
    ObjectGuid guid;
    bool opened = false;
    bool windowBusy = false;
    bool hasLoot = false;
};
```

### 3.3 Compatibility (overseer_core_compat.h)

```cpp
// AzerothCore version compatibility for LootRollDisenchant enum
#ifdef AZEROTHCORE_LT_2024_06_15
enum LootRollDisenchant { /* old values */ };
#else
using LootRollDisenchant = RollVote;  // NEED, GREED, PASS, DISENCHANT
#endif
```

---

## 4. Three Allocation Options Compared

### Option A: Need Before Greed by Role Fit (Current AzerothCore LFG Behavior)

**How it works**: `Group::NeedBeforeGreed` (line 1288) calls `CanRollForItemInLFG()` which checks:
- Item class/subclass matches player's armor type (plate/mail/leather/cloth)
- Item stats match player's role (tank/healer/dps) via primary stat
- Item is equippable by player's class

**Pros**:
- Already implemented in core (LFG dungeon finder uses this)
- Prevents "ninja looting" — warriors can't need on cloth, mages can't need on plate
- No configuration required

**Cons**:
- Only active in LFG (NeedBeforeGreed loot method), not in GroupLoot or MasterLoot
- Role detection is coarse (primary stat only, no off-spec support)
- Cannot be extended by modules — no hooks in the eligibility check
- Rigid: a ret paladin needing on healing gear is blocked

### Option B: Rotation / Round-Robin

**How it works**: Track last winner per group, next eligible item goes to next member. GroupLoot already does a weak form of this for unrolled items (`Group::GroupLoot` round-robin).

**Pros**:
- Simple, fair over long sessions
- Zero config
- Works with any loot method

**Cons**:
- Ignores item utility — a healer gets DPS trinket because "their turn"
- No concept of need vs want
- Hard to implement for modules — no hook at roll creation time to inject priority
- Round-robin in core only triggers when **all pass**; cannot intercept Need/Greed phase

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

1. **Already exists in core** — `CanRollForItemInLFG()` at Group.cpp:1300+ implements the logic. It just needs to be exposed to GroupLoot method, not only NeedBeforeGreed.

2. **Minimal module changes** — mod-overseer already tracks `ItemVia::Need` vs `ItemVia::Greed` in `OnPlayerGroupRollRewardItem`. The module can already *observe* the outcome; it just can't *influence* it.

3. **Solves the real problem** — The primary complaint in bot groups is "wrong armor type rolls need" (e.g., hunter needing on plate). Role-fit check eliminates 90% of this.

4. **Extensible path** — If a hook were added at `Group::GroupLoot` roll creation (before `SMSG_LOOT_START_ROLL`), a module could:
   - Filter eligible Need voters by role fit
   - Inject custom priority (EPGP, rotation, etc.)
   - Still fall back to core random roll for tiebreak

5. **No persistent state required** — Unlike EPGP, role-fit is computed from live character data (class, spec, equipped items).

### What Would Need to Change in AzerothCore

A single hook in `Group::GroupLoot` (around line 1150, before vote collection):

```cpp
// Proposed hook point
virtual bool OnGroupLootRollCreated(Group* group, Loot* loot, Item* item, std::vector<Player*>& eligibleNeeders);
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

- `src/server/game/Entities/Player/Group.cpp` — lines 1123, 1288, 1443, 1497, 1548, 1604
- `src/server/game/Scripting/ScriptDefines/PlayerScript.h` — `PLAYERHOOK_ON_GROUP_ROLL_REWARD_ITEM`
- `src/server/game/Scripting/ScriptDefines/GroupScript.h` — no roll hooks
- `src/server/game/Scripting/ScriptDefines/LootScript.h` — `LOOTHOOK_ON_LOOT_MONEY` only
- `src/mod_overseer.cpp` — line 7885 `OnPlayerGroupRollRewardItem`
- `src/overseer_decisions.h` — `LootedItem`, `ItemVia` enum
- `src/overseer_core_compat.h` — `LootRollDisenchant` compatibility