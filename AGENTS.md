# This repository is public

Everything here — code, commit history, issues, pull requests, comments,
review threads, discussions, releases, wiki pages, GitHub Pages content —
is visible to anyone on the internet, forever, including after deletion
(forks, caches, and search-engine indexes outlive an edit or a delete).
Treat every write to this repo, in any surface, as something a stranger
reads the moment you make it.

This file exists because that got violated repeatedly before it was
written down. Real personal names, a real home network's addresses, real
device identifiers, and a live unrotated credential all ended up in public
issue trackers and public git history — not through carelessness in the
code, but through ordinary conversational writing in issues, PR bodies, and
code comments, where the discipline that already existed for the shipped
code was never applied. This file is the fix: the same discipline,
extended to every surface an agent writes to, not just the diff.

## The rule

**Nothing that identifies a specific person, a specific private network,
or a specific credential may appear anywhere in this repository, in any
form, ever.** Not in code. Not in a code comment. Not in an issue body. Not
in a PR description. Not in a commit message. Not in a comment reply. Not
in a test fixture. Not "just this once because it's only in a closed
issue" — closed does not mean hidden, and neither does deleted.

This is broader than "don't commit secrets." A secret scanner catches an
API key. It does not catch a sentence like *"[a real first name]'s home
server, reachable at their usual address, needed a restart"* — nothing
there matches a secret
pattern, and it is exactly the kind of sentence that put a real name, a
real domain, and a real IP into a public tracker tonight. Write for a
stranger from the first word, not just the code.

### Concretely, never write any of the following into this repo, on any surface

- A real personal name — yours, a collaborator's, anyone's. Refer to
  people by role ("the maintainer," "the operator," "a reviewer") the same
  way this file does.
- A real hostname, domain, or subdomain that resolves to a private network
  or a real person's infrastructure (a home VPN suffix, a personal tailnet
  domain, a work-in-progress product's real URL before it's meant to be
  public). Use a placeholder that is visibly fake: `example.com`,
  `your-server.internal`, `<your-domain>`.
- A real IP address on any network you actually operate — home, cloud, or
  otherwise. Use an RFC 5737 documentation range (`192.0.2.0/24`,
  `198.51.100.0/24`, `203.0.113.0/24`) or an obviously fictional one
  (`10.0.0.X` as a *labeled example* is fine; a live address copy-pasted
  from a real `curl`/`dig`/log output is not).
- A real device identifier: a serial number, a MAC address, an IMEI, a
  hardware ID, an account ID, a database GUID tied to a live system.
- A credential of any kind, live or "already rotated" — a key, a token, a
  password, a signing certificate, a webhook URL with a token embedded in
  the path. "It's already been rotated" is not a reason to leave the old
  value visible; redact it anyway, because the *pattern* (which SSM path,
  which naming convention, which provider) is itself information.
- A path that reveals a real local username (`/Users/<name>/...`,
  `C:\Users\<name>\...`) or a real machine's hostname.
- A quote attributed to a specific named person, even an accurate one.
  Paraphrase instead: "the operator decided..." not "Alice said...".
- An `@`-mention of anyone who is not already part of the conversation.
  A mention notifies that account and subscribes it to the thread, and
  neither can be undone by editing or deleting the text. `@grug` in
  particular is an unrelated real user, not the review bot: the bot is
  `grug-tribe[bot]` and takes slash commands (`/grug improve` re-runs the
  code review, `/grug recheck` re-runs the plan check). To name a handle
  in prose, put it in backticks, which GitHub does not treat as a mention.
- The name of another private repository, service, or internal system
  that isn't itself meant to be discoverable. Cross-repo references
  belong in the *private* tracker, not migrated wholesale into a public
  one.

### If you are migrating or importing content

Content that already exists elsewhere — an issue being moved from a
private repo, a comment thread being copied in, history being subtree-split
into a new repo — is not exempt from this rule because it was written
before this file existed. **Migration is not a scrub.** Before content
from anywhere else lands in this repo, on any surface, re-read it against
every bullet above and rewrite what fails. If a whole issue's substance is
inseparable from the personal/private detail it's built on, don't migrate
it — summarize the generic problem it represents instead, or leave it out.

Wholesale-copying a private issue tracker into a public one because it was
"faster" is exactly how this happened the first time.

### If you find a violation already in the repo

Fix the current tree, then say plainly in your response that older
issues/PRs/comments/history may still carry it and that this needs a
human decision, not a silent edit-and-move-on. Do not delete or rewrite
someone else's public comment without asking first — you may not always
know why it was worded that way. Editing your own agent-authored content
to remove a violation is always fine and encouraged.

## The other half: this repo must be genuinely reusable

A stranger must be able to clone this repository, supply their **own**
configuration and secrets, and have it work — without reading anything
beyond the README and an example config file to know what to change.

- Every value specific to one deployment (a hostname, an IP, a region, an
  account ID, a device identifier) is a variable, an environment variable,
  or a config file entry — never a literal baked into source, a workflow
  file, or a script.
- Ship a `.env.example` / `config.example.*` alongside any file that reads
  real config, with every key present and an obviously-placeholder value
  (`YOUR_DOMAIN_HERE`, not a real one with the last octet changed).
- If a CI/CD pipeline assumes infrastructure that doesn't ship with the
  repo (a specific runner pool, a specific cloud account, a specific
  private reusable workflow), say so explicitly in the README rather than
  let a stranger discover it as a mysterious failure. "This requires your
  own self-hosted runner and your own AWS account" is an honest
  dependency; a silent reference to `uses: <this-operator>/infra-private/...`
  is not.
- Prefer this repo's own already-public reusable workflows
  (`quadseven/infra-public/...`) over hand-rolled CI where one already
  exists — they're already written to take config as input rather than
  assume it.

## Why this file, not just a smarter secret scanner

A pattern-matching scanner catches shapes: an AWS key, a PEM block, a
32-character hex string. It cannot catch a paragraph of ordinary prose
that happens to name a real person or describe a real network in plain
words — which is where nearly everything this file exists to prevent
actually showed up. Scanners still belong in CI as a backstop for the
shapes they *can* catch; this file is the layer above that, for the judgment
a scanner doesn't have.

## How work moves here

Several AI brains work this repo beside the operator: Claude Code, Muse, Dot and the Loom factory. Loom has a station that dispatches, a brain that briefs and merges, and lanes that build. No brain outranks another. Each acts under its own GitHub App, never a person's account, and signs posts `-- <name>`.

**Claims.** Put `claim:<name>` on a ticket before starting, and create the label if this repo lacks it. Check first and skip a ticket already held. One claim per ticket: the station keeps the earliest and removes the rest. A claim expires after 2 hours with no real activity, unless a comment holds it with `waiting: <what> until <time>`, or an open PR the claimer wrote and referenced holds it for up to 7 days. `claim:quadseven` is a person: leave those tickets alone. On stopping, push the work and say what is done, what is next, and the branch and head. Take over only an expired or plainly dead claim, after reading the holder's comments and open PRs.

**Labels.** Loom sets one state label per ticket:

`loom:triage` (not ready) -> `loom:queued` (no lane) -> `loom:briefed` + `lane:<name>` -> `loom:running` -> `loom:pr` -> `loom:merged` -> `loom:shipped` -> `loom:verified`, then closed.

`loom:ticket` marks every ticket. `loom:failed` means the runner gave up; the reason is on the ticket. `wait:*` names why a ticket is held. `needs:human` is an ask for a person. `loom:factory` marks a fault in the factory itself.

**CI and required checks.** Required checks differ per repo, so read them live: `gh api repos/<owner>/<repo>/rules/branches/<default-branch>`. Every repo needs a PR and `Grug - Chief`; some add test jobs; some add `Grug - Elder`. Run each command in `.loom/validation.conf` before pushing, and read the failing workflow file before guessing at a red check.

- Shared workflows come from `quadseven/infra-public`, pinned by full SHA.
- Leak scan: reads the diff and the PR title and body, and a scan that cannot run is red. It has 9 rules: tailnet hosts, tailnet IPs, private-range IPs, server names, node names, user paths, MAC addresses, the stray mention, and cross-repo refs. A ref of the form `name#N` or `owner/name#N` to any other repo fails, public or not, unless the leak scan's `allowed-repo-refs` input lists that repo. Bare `#N` means this repo. A line with `leak-guard-allow` is exempt and visible in review; use it only for a real false positive.
- Trust gate: a job on self-hosted workflows that admits collaborators only and can reject an App token. A repo with no self-hosted jobs has none.
- Other guards, where a repo calls them: workflow hygiene, size ratchet, CI config freshness, and close completeness, which reopens an issue closed with unticked boxes.
- Agent Apps cannot push `.github/workflows` changes.

**`grug`.** The bot is `grug-tribe[bot]`, driven by slash commands in a PR comment. Its handle takes no at-sign.

- `Grug - Chief` reads the PR body. It wants `## Why` (5+ words), `## Acceptance criteria` (3+ bullets), `## Out of scope`, `Size: XS|S|M|L`, and `Refs <owner>/<repo>#N` or `Refs #N` to a ticket that is an epic or sits under one. Text in HTML comments does not count. Closing keywords are out: tickets close when their boxes are ticked.
- A body with an agent marker (such as a session link) from an App author, or a Loom lane footer, gets a neutral Chief: `Hunt Plan skipped`. Neutral passes. A body with no marker gets the full check, so write the full shape every time.
- `Grug - Elder` reviews the code by LLM. `/grug improve` re-runs it. `/grug recheck` re-runs Chief only. Five pushes in ten minutes pause Elder until `/grug resume`.
- A degraded or partial review says `Degraded review (<reason>): not a verdict`. It is not a pass, whether it concludes failure or neutral. Run `/grug improve` once the backend recovers.
- `Grug - Guard` scans for security problems and is advisory unless the repo turns on blocking. Haunt, once called Sentinel, comments when a PR closes over an unresolved critical finding. It posts no check; answer its comment and re-verify.

**Merge gate.** Rulesets enforce less than this, and this binds: CI green, `Grug - Elder` success on the head (neutral is not enough), `Grug - Chief` success or neutral, not behind base, every bot thread answered, and a test that fails without the fix. `gh pr checks` does not show threads, so read `reviewThreads` by GraphQL and `pulls/<n>/reviews`. Anyone merges what they built. Outside shadow, which today means `quadseven/loom` and `quadseven/grug`, Loom merges docs, test and app code tiers alone and holds deploy, security and cross-repo tiers for the operator. Every other Loom repo is in shadow: Loom runs every gate and records each merge or close for the operator instead of taking it, until the operator graduates the repo. A change to how brains merge, close or rewrite needs a fleet chat line and a 5 minute wait. Never use `--admin`.

**Human line.** People do: permanent deletion of records, creating, rotating or entering credentials, access grants, new spending, accepting terms. People decide: another brain's role and the fleet goal text. A second brain must agree, in writing, to clear a lane demotion, lift another brain's hold, or reopen a sound close. Everything else needs nobody. Ask with `needs:human` and one exact question. A relayed yes is not a yes. Never print a credential.

**When stuck.** Labels and stages: `docs/BRAIN.md` in `quadseven/loom`. Reusable workflows: `docs/reusables.md` in `quadseven/infra-public`. Chief's rules and every `/grug` verb: the README in `quadseven/grug`, whose `make chief-check` tests a body before posting. Fleet log: issue 30 in `quadseven/loom`. File a factory fault there as `loom:factory`. Poll `gh` at most once per 5 minutes.

<!-- Everything above the next line is synced from quadseven/infra-public and replaced on every sync. Put this repo's own content below it. -->
<!-- repo-specific below -->

## This repo

The Overseer module: a C++ AzerothCore module that runs the bot families' decisions on the realm.

- **Required checks:** `decisions`, `adapter`, `adapter (heads)` and `Grug - Chief`. Elder also reviews every PR, and the merge gate above needs it.
- **Local checks:** `bash scripts/check-locally.sh decisions` runs every decisions test binary and is the lane check in `.loom/validation.conf`. `bash scripts/check-locally.sh adapter` compiles the adapter in a container; lanes do not have that toolchain.
- **A fix needs a test:** `tests/test_*.cpp` for decisions, and a `_source` test when the wiring lives in the adapter. Prove it fails on main.
- **Shipping:** a merge here changes nothing live. A roll pins this repo's SHA into a paired image build, and the steps live in `docs/ROLLS.md` in the overseer app repo. Rolls are routine.
- **Loom:** this repo is in shadow, so Loom records merges and closes rather than taking them. Brains merge what they built.
- **Public repo:** the rule below binds every file, commit, PR and comment.


## Working on this module: things learned the expensive way

None of this is policy. It is the set of mistakes that have actually been
made here, written down so they are made once.

### Always fix the code. Never reach for an admin shortcut.

When characters are stuck, an operator-level command will almost always unstick
them faster than a patch will. Do not use it. A GM revive, a GM teleport, a hand
edit of a row the server owns - each one removes the evidence that something is
broken and buys a few minutes at the cost of the fix.

This is the standing instruction from the operator, and it has already paid for
itself: characters wedged as ghosts after a wipe looked like a case for a
one-off revive, and chasing the code instead found that `Player::GetCorpse()` is
map-scoped, so every character that releases out of an instance was invisible to
the recovery drive and skipped forever. A revive would have cleared the symptom
and left that in place for every future wipe.

The corollary is that "stuck" is a bug report, not an inconvenience. Measure it,
find the line, and change the line.

### `delivered` is not `done`. Read the world back.

The command surface reports that a command reached a character, not that
anything happened. Two verbs have been observed returning `delivered` with a
null result while changing nothing at all: dropping a quest by id, and sending
a character home. Both looked like clean successes in every table.

This is not a bug in the reporting so much as a fact about the target: much of
what a bot is asked to do ends in a call that reports its failure to a *client*,
and a bot has no client to show it to. `Trainer::TeachSpell` returns void and
does exactly this, which is why the learn path already asks `HasSkill` afterwards
instead of trusting the call.

**So: after any command that should change the world, query the world.** A test
that asserts on command status is asserting on a postmark, not delivery.

### An areatrigger cannot fire for a character with no client

The teleport at an instance portal is reached from `CMSG_AREATRIGGER`, a packet
the *client* sends when it notices it has touched one. The server never sweeps
player positions against triggers. A bot can therefore stand exactly on a portal
forever and nothing happens - measured, with four characters at 5, 7, 10 and 10
yards from a trigger of radius 7.

Walking in "like a player" turns out to depend on a part of the player a bot does
not have. The packet has to be sent deliberately, to the same handler, which
re-checks the radius itself so nothing can be teleported that did not walk there.

### Arrival tolerance depends on what is being arrived at

A creature is found via its spawn point while it patrols away from that point, so
the tolerance has to absorb the gap. An aimed position does not move, and the
thing standing on one is often an areatrigger *smaller* than a creature-sized
tolerance. Arriving "successfully" outside the thing you were sent to is worse
than not arriving: the errand completes, the drive that sent you resumes, and the
character walks away from the door it was touching.

### The random-bot gate is the first thing to suspect

`PlayerbotFactory::InitTradeSkills` opens with a return if the bot is not a random
bot. So do several other upstream systems. Roster characters use named accounts
and are therefore never random bots, so any upstream feature that "just does not
run for our characters" is worth checking against that gate before anything else.
It has now been the answer for professions, talents, bag grants, trainer spells,
the dungeon finder, and the dead-bot rescue.

### Editing the C++ from a script will rewrite every line ending

The source is LF. A naive read-modify-write from a script on a Windows checkout
turns the whole file CRLF, and the diff is then thousands of lines instead of the
handful you meant. Read bytes, normalise deliberately, restore the original
ending on write - and check `git diff --stat` before committing. A 6,000-line
diff for a one-line change is the tell.

### Zones are not levels, and travel does not know that

An aim across a continent is routed without regard for what it crosses. Sending a
level 17 party from one safe zone to another has put them through a level 20-30
zone and, unaided, into a level 50-58 one. Deaths went from a six-hour quiet
streak to 24 in fifteen minutes.

Aim the party **leader** only, and watch the death table while it walks. If deaths
exceed roughly three in five minutes, clear the aim - the destination is not worth
the crossing.

The opposite lever, for when the party is dying where it already is: write
`guard:<map>:<x>,<y>,<z>` into each roster row's `travel_npc`, in whole yards
(the column is 32 characters), instead of an `at:` aim. An `at:` aim is an errand
like any other and was released, written over and called off as lethal within
minutes of being applied (#163). A guard is walked to and held: no release,
claim or death count in this module ends it, and the quest drive stands down for every
character carrying one, followers included. The log says when each guard is
first seen and when it is cleared. Clear the column by hand to end it; the quest
drive takes the character back on its next poll.

### "Is there ground here" and "which ground" are two questions

The map's height field under and beside Stormwind is a flat placeholder at z
59.457, the city is models above it, and the navmesh has polygons on the
placeholder because it is built from that terrain. A character that leaves the
city floor lands on a plane every "is there ground" instrument agrees is ground:
Detour finds a polygon at its feet, the footing fan holds on flat ground, and
the terrain drive once logged that as the detector being wrong while the leader
rode two thousand yards under the city and the sea beside it. Measure it before
believing a reading: the raw terrain height (`Map::GetGridHeight`) at the
character's own x and y, and a ring of it 25 yards out. Feet on the terrain and
a ring that does not move is the plane, whatever a navmesh says
(`OverseerDecisions::OnTheHiddenPlane`).

### Combat wedges travel

A character can sit at full health, in combat, not dying and not moving, with a
valid travel errand it never executes. Removing the strategies that make it pick
fights is what unsticks it; the errand then runs.

### A rejoin is proved by one log line, and the log is short

A member that dies inside a dungeon and is released to a graveyard outside is
walked back in under its own power. The proof that it worked is one line in the
worldserver log:

    stranded outside map <map> - <n> went back in this poll

It is printed only when a member actually changed map, so it is a measurement
and not a postmark, on the same terms as "delivered is not done" above. The
three lines around it are the whole recipe, in the order they appear:

- `dungeon run <n> is short a member and is WALKING IT BACK to areatrigger <id>`
  - once per run, the moment the census finds somebody outside. This is the
  hint that there is anything to look for.
- `'<name>' walked into area trigger <id> and is now on map <map>` - the member
  standing in the door, put through by the knock.
- `dungeon run <n> knocked on areatrigger <id> for the <k> stranded outside map
  <map> - <j> went back in this poll` - the proof line, and the census is that
  much nearer complete.
- `dungeon run STAGED_INSIDE satisfied - all <n> roster members are on map
  <map> together` - the census reached whole, and the run moved on to CLEARING.

**THE WINDOW IS THE HARD PART.** The node the dev realm's worldserver runs on
rotates its log, and on 2026-10-18 the readable window was about eighty
minutes (15:52 to 17:14 UTC). A check that finds nothing has only covered that
window, and it is worth saying which one it covered. What the check has found
so far: that window had no such line - the one family run in it ended
`client_lost` after seven minutes, with no member death inside it - and the
window of 2026-10-20 13:24 to 15:22 UTC had the walk running and never
finishing, because the lease ended when the member stood beside its group
leader rather than when it reached the run's map. That second one is what
#1087 fixed, and what `tests/test_dungeon_rejoin.cpp` pins; the wiring the
walk depends on is pinned by `tests/test_dungeon_rejoin_source.cpp`.

The rotated logs are archived on the worldserver's node, so the window is wider
than the live file. The search of 2026-10-24 (#1108) covered every archived
worldserver log from 2026-10-20 02:20 to 2026-10-24 02:31 UTC, gaps between pod
restarts aside, and **the proof line is not in it - not once**, and neither is
the `walked into area trigger` line. The walk was started seven times after
#1087 was deployed (2026-10-20 18:45 UTC). Six were closed by the 15-minute
census ceiling (`cannot assemble its census ... The attempt is CLOSED`) and one
was abandoned 15s in by the full-bags gate. Five of those six were not this
case at all: the dungeon finder took part of a family in and the rest never
entered, so nobody had died inside.

The line counts for that window, by the text matched:

- `stranded outside map <map> - <n> went back in this poll`: 0
- `walked into area trigger`: 0
- `is short a member and is WALKING IT BACK`: 7, one per walk started
- `cannot assemble its census`: 6

The one that was this case, 2026-10-22 12:17 to 12:34 UTC (run 2 of campaign
101, map 90, areatrigger 324): a member died inside at 12:19:27, was released
on its behalf at 12:21:34, and the walk began at 12:21:35 under its own lease
as #1087 meant it to. It then took the spirit healer at 12:22:30, and the
revived-sickness hold (#746) held it still from 12:22:47 to 12:32:34 - ten of
the census's fifteen minutes - taking over the walk it was on. The census
closed at 12:33:32 with that member still outside. So the walk-back has not yet
been seen to work live, and the sickness hold is the thing that stopped it.

The ordering half held: the coordinator printed no `STAGED_INSIDE satisfied`
and no `dungeon run CLEARING` line for that family while it was short, and the
run went to RESET, not CLEARING. Two things in the window look like counter
examples and are not. `STAGED_INSIDE satisfied` carries no run number, and the
ones printed in the same minutes name map 189, another family's run, so match
on the map. And the per-member `'<name>' is inside map <map> (run <id>) -
issuing 'dc on'` lines at 12:18:29 come from the finder-group re-entry path
after a restart, not from the coordinator entering CLEARING.

So the check still needs a member to die inside a run and walk back without
taking the spirit healer, or with a hold short enough to leave the census time.
It cannot be scheduled from here: the log is on the realm and the realm is not
reachable from a repository checkout.

### The pin and the submodule move together

The deployment repo pins this module by SHA and also carries a submodule at the
same path so its tests can read this source as text. Bumping one without the
other leaves the two disagreeing silently. The pin file says so; it is easy to
read past.

### Shipping a change end to end

A merge here does nothing on its own. The change reaches a world through a pin
bump in the deployment repo, then an image build, then a digest move in that
world's manifest. The image build is the slow part, and it is slow: measured
over all 37 successful runs in the workflow's history, the job takes 36 to 88
minutes (median 60), and the wall clock from push to answer is 35 to 129
minutes (median 66) once queueing is counted.

This paragraph used to say six minutes, and before that a comment elsewhere
said forty-five. Both were written by somebody who had measured, and both went
stale without anybody noticing, which is the actual lesson: a duration written
in a document is a reading from one day, not a property of the build. Measure
it before planning around it rather than trusting the last person who did,
this paragraph included.

The PR body gate is strict and worth writing for on the first attempt: it wants a
`## Why`, an `## Acceptance criteria` or `## Test plan`, a literal `Size:` line, an
`## Out of scope`, and an issue link. It also refuses a `Closes #N` whose issue
still has unticked boxes, which is usually correct - if the issue asks for four
things and the PR does one, the link is `Part of`, not `Closes`.
