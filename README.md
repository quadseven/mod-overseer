# mod-overseer

An AzerothCore module that gives [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
a control plane: a `overseer_command`/`overseer_roster` database bridge that
lets an external system (Discord, a web dashboard, whatever) drive named bot
characters — quest aiming, travel, professions, materials, party leadership,
job scheduling, POV streaming support, death recording, and more — without
touching the mod-playerbots source itself.

## Where this came from

Extracted 2026-08-27 from [quadseven/infra](https://github.com/quadseven/infra)
(`production/docker/azerothcore-playerbots/mod-overseer/`), with full commit
history preserved via `git subtree split`, so it can be built and versioned the
same way the other AzerothCore modules already are — a standalone repo, pinned
by commit SHA, cloned into `modules/` at build time. It was always structured
this way internally; this just makes the structure match the pin.

## Layout

- `include.sh` — the module's entry point; AzerothCore's build globs `modules/*/`
  and uses this to discover a module, the same as every other module here.
- `src/mod_overseer.cpp` — the whole module. One file, deliberately — see the
  file's own top-of-file comments for why.
- `data/sql/characters/base/` — dated migrations against the `characters`
  database, applied in filename order at container startup. Each migration is
  guarded: a schema older than a given column degrades rather than crashing,
  so a build can run against a database that hasn't caught up yet.

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
