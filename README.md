# mod-overseer

An AzerothCore module that gives [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
a control plane: a `overseer_command`/`overseer_roster` database bridge that
lets an external system (Discord, a web dashboard, whatever) drive named bot
characters — quest aiming, travel, professions, materials, party leadership,
job scheduling, POV streaming support, death recording, and more — without
touching the mod-playerbots source itself.

## Where this came from

Extracted 2026-08-27 from its original deployment tree, with full commit
history preserved via `git subtree split`, so it can be built and versioned the
same way the other AzerothCore modules already are — a standalone repo, pinned
by commit SHA, cloned into `modules/` at build time. It was always structured
this way internally; this just makes the structure match the pin.

## Layout

Dungeon finder returns can place members on their original continents. An exit
and reset evacuation finish when every visible member has left the source
dungeon map. Entry still requires the destination dungeon and the leader's copy.

- `include.sh` — the module's entry point; AzerothCore's build globs `modules/*/`
  and uses this to discover a module, the same as every other module here.
- `src/mod_overseer.cpp` — the whole module. One file, deliberately — see the
  file's own top-of-file comments for why.
- `data/sql/characters/base/` — dated migrations against the `characters`
  database, applied in filename order at container startup. Each migration is
  guarded: a schema older than a given column degrades rather than crashing,
  so a build can run against a database that hasn't caught up yet.
- `conf/mod_overseer.conf.dist` — the module's configuration, and the example
  config file. See below; there is very little of it on purpose.

## Configuring it

Almost nothing here is configurable, and that is deliberate: most of what this
module decides it works out from the database at the moment it is asked, and a
setting duplicating something the database already knows would be a second
answer that could disagree with the first.

What IS configuration is the handful of values that are a **decision somebody
made about one deployment** rather than a measurement — today, two groups of
them:

- the guild **recruit policy**: how big the guild is recruiting toward, and the
  two ends of the level range it will ask inside;
- the guild **raid shape**: how many of a forty-man raid's seats tank and how
  many heal, plus one safety catch on the irreversible half of `guild raid
  form`.

Nothing in a character database knows any of those. Everything else about a
raid layout (which class takes which seat, who is picked first, which of groups
1 through 8 anybody ends up in) is derived from those two numbers and the
guild's own roster, so it is a rule rather than a setting.

Every key is optional and has a compiled default, so a worldserver that has
never seen `conf/mod_overseer.conf.dist` behaves exactly as one that copied it
unedited. Copy it to `mod_overseer.conf` in your worldserver's module config
directory and uncomment a line only to disagree with a default.

**The core does not fall back to a `.conf.dist`.** If the copy is not made, the
worldserver logs a missing-file line at startup and the module runs on its
compiled defaults — harmless in effect, but none of the settings are then
visible or tunable without a rebuild.

## Building it

This module has no build of its own — it compiles as part of an AzerothCore
worldserver, alongside mod-playerbots and whatever other modules are present.
See the consuming repo's build workflow for the actual pipeline; the short
version is: clone this repo to a pinned SHA into `modules/mod-overseer`, then
build AzerothCore normally.

## Consuming it (the pin)

Pin by commit SHA, not a branch — `main` moves. Whoever consumes this should
verify the module actually built after bumping the pin (a compile is not a
proof it works) before trusting it in whatever the module ends up steering.

## A real build dependency, not just source

This module compiles against small patches to the pinned core and
`mod-playerbots` that live under `patches/` here. `apply-patches.sh` applies
them at build time. `patches/README.md` explains the mechanics. In particular,
`azerothcore-wotlk/0001-send-cast-result-hook.patch` supplies the cast failure
hook consumed by this module. Query recent failures with:

```sql
SELECT character_name, subject_id, subject_name, detail, occurrences
FROM overseer_event
WHERE kind = 'cast_fail' AND detail = 'SPELL_FAILED_EQUIPPED_ITEM_CLASS'
ORDER BY last_seen DESC LIMIT 50;
```

For `mod-playerbots`, `0005-wander-npc-can-be-aimed.patch` is the patch this module's own source
actually calls into (`NewRpgInfo::WanderNpc::npcEntry`,
`ChangeToWanderNpc(entry, pos)`) — upstream mod-playerbots has neither. If you
bump the pinned `mod-playerbots` SHA and a patch stops applying, that is
usually good news: upstream likely merged the same fix. Delete the patch, or
rebase it — see `patches/README.md`.

## Contributing

`main` requires a pull request — direct pushes are rejected (deletion and
force-push are blocked too). This is enforced by a GitHub Repository Ruleset,
managed outside this repository.

### Failed dungeon exits

When walking out fails, the hearth recovery keeps the original run state.
Once every family member is observed outside, normal run finalization records
the outcome and advances the same campaign. An unseen member is not proof
of an exit; a member still inside remains in recovery.
