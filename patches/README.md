# Local patches against the pinned upstream trees

`UPSTREAM-PINS.env` pins five upstream repositories by SHA and the build
compiles them **verbatim**. That is the right default, and it is why the image
is reproducible - but on its own it means a bug in upstream's code has exactly
two outcomes: open a PR upstream and wait, or live with it.

This directory is the third option. Each file here is a small, exactly-matching
diff applied to a pinned tree at build time, in
`build.azerothcore-playerbots.yml`'s "Apply local patches" step, after the
clone and before the compile.

A patch here is **temporary by construction**. It exists to hold a fix until
upstream takes it, and it is designed to break loudly the moment it stops being
needed.

## Layout

```
patches/
  <upstream-tree-name>/
    NNNN-short-description.patch
```

`<upstream-tree-name>` is exactly the directory the build clones that repo
into, which makes the target unambiguous:

| directory                     | tree it patches                          |
| ----------------------------- | ---------------------------------------- |
| `patches/azerothcore-wotlk/`  | the core fork (`AC_CORE_*`)              |
| `patches/mod-playerbots/`     | `modules/mod-playerbots` (`AC_MODULE_*`) |
| `patches/mod-ollama-chat/`    | `modules/mod-ollama-chat`                |
| `patches/mod-dungeon-clear/`  | `modules/mod-dungeon-clear`              |
| `patches/mod-junk-to-gold/`   | `modules/mod-junk-to-gold`               |

Only directories that hold at least one patch exist. `apply-patches.sh` refuses
a directory whose name does not match a tree in the build context, and refuses
a directory that holds no `*.patch` file, so a typo or a leftover cannot become
a silently-skipped patch.

Patches apply in filename order within a directory, hence the `NNNN-` prefix.

`mod-overseer` is ours, but as of infra#2929 it lives in its own repo
(quadseven/mod-overseer), pinned the same way as `mod-ollama-chat`/
`mod-dungeon-clear`/`mod-junk-to-gold` above - edit it there and bump
`AC_OVERSEER_SHA` in `UPSTREAM-PINS.env`; patching it here is rejected.

## Adding a patch

1. Fetch the tree at the pin it will be applied to:
   `git clone <repo> && git checkout <SHA from UPSTREAM-PINS.env>`
2. Make the change, then `git diff > NNNN-name.patch`.
3. Prepend the WHY block (see below) above the `--- a/` line. Everything before
   the first diff header is ignored by `git apply`, so it is free-form prose.
4. Drop it in the right directory. **Nothing else needs editing** - not the
   build workflow, not the check workflow, not `apply-patches.sh`.
5. `production/docker/azerothcore-playerbots/verify-patches.sh` proves it
   applies against the current pins **and that it changes bytes**. It rebuilds
   the build's exact tree layout and performs a real apply into a throwaway
   workdir, because a dry run cannot tell "would change this file" from "never
   looked at this file" (see infra#2849 below). The PR check runs the same
   script; `tests/test-apply-patches.sh` covers the script's own guard rails
   with no network at all.

## What every patch must carry

The diff says *what*. The header must say *why*, and above all **what would let
this file be deleted** - because these files are meant to be deleted, and a
patch nobody knows how to retire outlives its reason. Use the existing patches
as the template:

- `WHY THIS PATCH EXISTS` - the defect, cited to file:line at the pinned SHA,
  and a link to the analysis in `plans/` if there is one.
- `WHAT WOULD LET THIS PATCH BE DELETED` - the upstream change that retires it,
  and the upstream PR link once one is open.
- `APPLIES TO` - repo, SHA, and the function or region touched.

## When a patch stops applying

The build fails. On purpose. See the header of `apply-patches.sh` for why
`git apply` was chosen for exactly this property, and never `patch -p1`.

> **That was not true between 2026-08-17 and 2026-08-25 (infra#2849).**
> `git apply` resolves a patch's paths against the top level of the repository
> it *discovers*, not the directory it is pointed at with `-C`. The build
> deletes each module's `.git`, so git walked up, found the **core's**
> repository, and gave itself a path prefix of `modules/mod-playerbots/`.
> Every path outside that prefix was **skipped** - announced on stderr, and
> then **exit 0**, which `git apply --check` did too. Whether a patch's paths
> landed inside the prefix depended on whether it carried a `diff --git`
> header, so `0001`/`0002` applied and `0003`/`0004`/`0005` did not, for eight
> days, while both the build and the PR check reported all five applied.
>
> Nothing about the patch *files* was wrong, and nothing about them needs to be
> uniform: `apply-patches.sh` now pins `git apply` to the tree it was given
> (`GIT_CEILING_DIRECTORIES`), asserts that worked, treats any `Skipped patch`
> announcement as a hard failure, and - the check that does not depend on
> knowing any of this - **hashes every file a patch names before and after and
> fails unless the bytes moved**. Both header formats are fine. Do not "fix"
> this by reformatting patches.

A stale patch means upstream changed the code underneath it - very often by
taking the same fix. The only two correct responses are **delete the patch** or
**rebase it onto the new pin**. Do not add fuzz, do not add `|| true`, and do
not skip the step: a patch that quietly stops doing anything looks identical to
one that is still working, and the bug comes back with nobody watching.

**`mod-playerbots/0005-wander-npc-can-be-aimed.patch` is the one where deleting
is the wrong answer.** 0001-0004 fix upstream behaviour, so dropping one costs a
feature but still builds; 0005 ADDS an overload that `mod-overseer` calls
(`DriveTravel` -> `ChangeToWanderNpc(entry, pos)`), so dropping it breaks our own
compile 45 minutes into a build on `main`. Rebase it, or delete it and adapt the
call site in `mod_overseer.cpp` in the SAME commit. `UPSTREAM-PINS.env` carries
the full argument, and it is the file you are more likely to have open.

**`mod-playerbots/0006-wander-random-has-a-home-anchor.patch`** fixes
RPG_WANDER_RANDOM's random-walk-with-no-leash: `MoveRandomNear()` took a
`center` parameter that was never read, so every wander step originated from
the bot's own live position with nothing bounding how far the accumulated
walk could drift - which is how an under-20 family ended up standing next to
an elite in a high-level zone with nothing chasing them. Like 0001-0004, this
is a straight upstream-behaviour fix, not an added API: `mod-overseer` calls
nothing this patch adds, so dropping it (because upstream took an equivalent
fix, or because the pin moved and it no longer applies) costs the bound, not
the compile. Rebase it onto the new pin, or delete it once upstream's own fix
lands. See the patch's own header for the full trace to `NewRpgBaseAction.cpp`
and `NewRpgAction.cpp` at the pinned SHA.
