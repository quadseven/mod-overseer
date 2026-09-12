# Guild bank deposit/withdraw — design (infra#2831)

## Where this sits today (verified 2026-09-12)

Of the three things asked for tonight — "professions, personal bank, guild bank" —
only guild bank is actually missing:

| Capability | State | Evidence |
|---|---|---|
| Learn a profession | **WORKS** | `overseer_roster.learn_skill` + `travel_npc='profession trainer'`, driven by `professions.py::plan()` (bridge.py) and the trainer-arrival handler in `mod_overseer.cpp` (`TrainerStartedSkills` etc.) |
| Personal bank deposit | **WORKS** | Shipped in #207 ("bank one named item through the core's own bank handlers"). `BankerInReach` + the deposit driver around `mod_overseer.cpp:19960-20070` move an item via the core's own inventory functions, not simulated client packets. |
| Guild bank deposit/withdraw | **MISSING** | `wealth.py`'s own `GUILD_STEPS` capability table already tracks this: "travel to a guild banker" = works (`travel.py` ROLES resolves `"guild bank"` → `UNIT_NPC_FLAG_GUILD_BANKER`), "deposit into the guild bank" / "withdraw from the guild bank" = not written. |

**One correction the next person should make**: `wealth.py`'s `GUILD_STEPS` also lists
"buy a charter" / "collect the signatures" / "register the guild" as MISSING and its
`NO_GUILD_LEAD` text says "There is no guild, so there is no guild bank." Both are now
stale — the family has been in guild "Cave" (guild id 23) since #414/#434 landed
2026-09-11/12. The guild-formation road may have been walked by hand (GM/SQL) rather
than through an in-game charter flow the module drove, which is worth confirming
before rewriting that panel, but the "no guild" framing is wrong as of tonight and
will read as false to anyone checking the wealth page now.

## Why guild bank deposit isn't just "reuse the personal bank code"

The personal-bank driver (#207) moves an item between two storage locations owned by
the SAME player object (`Player::GetItemByPos` bag/bank slots), which is why it can
lean on the core's own inventory-move functions directly. A guild bank is a different
object — `Guild` (and its `BankTab`s) in AzerothCore, not `Player` storage — with its
own slot/tab model, per-rank permission gates (`GUILD_BANK_RIGHT_DEPOSIT_ITEM`,
withdraw caps per rank per tab), and its own log (`guild_bank_eventlog`). The
`BankerInReach` / `Unit::IsBanker` NPC-reach logic in #207 IS reusable as-is — a guild
banker is still just an NPC with `UNIT_NPC_FLAG_GUILD_BANKER` — but the actual
deposit/withdraw call needs `Guild`'s own API (`Guild::SwapItems` for items,
`Guild::HandleMemberDepositMoney` / the guild-bank money-move path for gold), not
`Player::StoreItem`.

## Scope for a first slice (deliberately narrow)

**Deposit-only, gold-only, v1.** Reasons:
- Gold has no slot/tab bookkeeping — no permission-rank matrix to get wrong on a
  first pass, no "which of the guild's bank tabs" decision.
- Deposit is inherently safer than withdraw: a wrong deposit still leaves the money
  inside the guild (recoverable by an operator with guild-master rights), a wrong
  withdraw could hand guild funds to the wrong character or amount.
- It proves the whole pipeline end to end (Python planner → standing aim → C++
  arrival handler → real `Guild` API call → verifiable in `guild_bank_eventlog` and
  the `guild.BankMoney` column) before any item/tab logic gets added.

**Explicitly deferred, in order of likely next value:**
1. Item deposits (needs tab selection — start with "whichever tab has free slots and
   the character's rank can deposit into", the same kind of resolvable-not-hardcoded
   rule `NearestBanker` uses for picking an NPC).
2. Withdrawals of any kind (money or item) — needs the per-rank permission read
   (`guild_bank_right`/`guild_bank_tab_right` tables) respected on the Python planning
   side before an aim is even written, so the module never asks the core to do
   something the guild's own rank rules would refuse.
3. Guild bank tab contents becoming visible on the wealth page (`build_guild_bank` in
   `wealth.py` currently has nothing to read even after deposits start working —
   `guild_bank_tab`/`guild_bank_item` tables are queryable read-only wins that don't
   depend on any of the write-side work above).

## Shape of the fix, following the profession-trainer pattern exactly

1. **Python planner** (new `guildbank.py`, or a section of `wealth.py`/`professions.py`
   if small enough — decide once the deposit RULE is written): decides which
   character, holding how much gold above some floor (mirroring whatever threshold
   `disposition.py`/`professions.py` already use for "this is surplus, not
   float-money-a-character-needs"), should deposit into the guild bank. Writes a
   standing aim: `travel_npc='guild bank'` (already resolves via `travel.py`) plus a
   new roster column or reuse of an existing "what to do on arrival" mechanism — check
   whether `overseer_roster` needs a new column (`guild_bank_deposit_gold` or similar)
   the same way `learn_skill` is a dedicated column for the trainer errand, rather than
   overloading `travel_npc` itself to carry both "where" and "what."
2. **C++ arrival handler**: extend the existing NPC-arrival dispatch (wherever
   `travel_npc == "guild bank"` / the resolved `guild banker` role is read on arrival —
   find this by tracing how `"profession trainer"` and `"banker"` each get a distinct
   on-arrival action today) to call `Guild::HandleMemberDepositMoney` (or whatever the
   3.3.5a `Guild` class actually exposes — **verify the exact signature against a real
   core checkout before writing this call**; I did not have one available in this
   session and am not going to guess a member function signature into a PR, per this
   project's own standing rule about confident-wrong guidance being worse than an
   admitted gap).
3. **Idempotent re-assertion**, same as `_write_trade_errand`: a worldserver restart
   should not lose an outstanding deposit intent, and a deposit that already happened
   should not be re-attempted.

## What this session did NOT do, and why

Did not write the C++ call itself. This box has no local AzerothCore core checkout to
verify the `Guild` class API against (`git log`/`find` turned up none), and the
adapter (`mod_overseer.cpp`) cannot be compiled standalone here regardless (confirmed
project constraint — it needs a full core+module build). Writing a plausible-looking
`Guild::` call without verifying it against real headers is exactly the failure mode
this project's own memory warns about repeatedly ("confident WRONG guidance beats
missing guidance for danger") — better to hand the next implementer a specific,
narrow, verified-safe target than a guess that reads as done.
