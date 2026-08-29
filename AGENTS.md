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

## Working on this module: things learned the expensive way

None of this is policy. It is the set of mistakes that have actually been
made here, written down so they are made once.

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

### Combat wedges travel

A character can sit at full health, in combat, not dying and not moving, with a
valid travel errand it never executes. Removing the strategies that make it pick
fights is what unsticks it; the errand then runs.

### The pin and the submodule move together

The deployment repo pins this module by SHA and also carries a submodule at the
same path so its tests can read this source as text. Bumping one without the
other leaves the two disagreeing silently. The pin file says so; it is easy to
read past.

### Shipping a change end to end

A merge here does nothing on its own. The change reaches a world through a pin
bump in the deployment repo, then an image build, then a digest move in that
world's manifest. The build is around six minutes, not the forty-five an older
comment claims - measure it before planning around it.

The PR body gate is strict and worth writing for on the first attempt: it wants a
`## Why`, an `## Acceptance criteria` or `## Test plan`, a literal `Size:` line, an
`## Out of scope`, and an issue link. It also refuses a `Closes #N` whose issue
still has unticked boxes, which is usually correct - if the issue asks for four
things and the PR does one, the link is `Part of`, not `Closes`.

## Why this file, not just a smarter secret scanner

A pattern-matching scanner catches shapes: an AWS key, a PEM block, a
32-character hex string. It cannot catch a paragraph of ordinary prose
that happens to name a real person or describe a real network in plain
words — which is where nearly everything this file exists to prevent
actually showed up. Scanners still belong in CI as a backstop for the
shapes they *can* catch; this file is the layer above that, for the judgment
a scanner doesn't have.
