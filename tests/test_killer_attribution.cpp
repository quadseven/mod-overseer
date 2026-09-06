/*
 * Who the kill hooks named, decided without a world.
 *
 * The live gap this pins cost a working day. On 2026-09-06 the family's falls
 * were traced to a hand-rolled fall on every dismount, that was fixed and
 * deployed at 17:19 UTC, and the fix was declared confirmed. Bork then died
 * three more times - 17:41:45, 17:45:09 and 17:51:13 - and every one of those
 * rows read `killer_type='player'`, `killer_entry=0`, `killer_name='Bork'`.
 * That was read as "attributed to himself, so not environmental", twice, by
 * two different readers. The core's own falling counter went 28 to 31 over the
 * same three deaths.
 *
 * The row was not ambiguous. It was WRONG, because the module wrote 'player'
 * for a death the core had routed through the player-kill hook with the victim
 * in both slots. Player.cpp:853 deals environmental damage as
 * `Unit::DealDamage(this, this, ...)`, and Unit.cpp:14298-14306 fires
 * OnPlayerPVPKill on any Player-on-Player kill with no `killer != victim`
 * guard. So EVERY environmental death - fall, drowning, fatigue, fire, lava,
 * slime, out of bounds - arrives looking exactly like a duel loss.
 *
 * So these cases are about what a column is allowed to CLAIM. "Another player
 * killed it", "it killed itself" and "nothing named a killer" are three
 * different findings, and the first two were folded together.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using OverseerDecisions::KillerKind;
using OverseerDecisions::KillerKindName;
using OverseerDecisions::NameTheKiller;

namespace
{

int failures = 0;

void CheckKind(char const* what, KillerKind got, KillerKind want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, KillerKindName(got),
                KillerKindName(want));
    ++failures;
}

void CheckName(char const* what, char const* got, char const* want)
{
    if (std::strcmp(got, want) == 0)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got, want);
    ++failures;
}

// The three deaths that were misread, exactly as the hooks delivered them.
void ADeathNamingItsOwnVictimIsSelfInflicted()
{
    CheckKind("Bork 17:41:45", NameTheKiller(true, "player", "Bork", "Bork"),
              KillerKind::SelfInflicted);
    CheckKind("Bork 17:45:09", NameTheKiller(true, "player", "Bork", "Bork"),
              KillerKind::SelfInflicted);
    CheckKind("Bork 17:51:13", NameTheKiller(true, "player", "Bork", "Bork"),
              KillerKind::SelfInflicted);

    // And it is the same finding for every environmental type, because the
    // hook cannot tell them apart and neither may this.
    CheckKind("a drowning reads the same way",
              NameTheKiller(true, "player", "Ugga", "Ugga"),
              KillerKind::SelfInflicted);
}

// The finding this separates it FROM, which must survive unchanged.
void ARealPlayerKillIsStillAPlayerKill()
{
    CheckKind("another player did it",
              NameTheKiller(true, "player", "Grug", "Bork"), KillerKind::Player);
    CheckKind("a duel loss is a player kill",
              NameTheKiller(true, "player", "Og", "Grog"), KillerKind::Player);
}

// The other two findings, kept apart from both of the above.
void ACreatureKillAndAnAbsentHookAreTheirOwnAnswers()
{
    CheckKind("Southsea Privateer 17:53:07",
              NameTheKiller(true, "creature", "Southsea Privateer", "Bork"),
              KillerKind::Creature);

    // A creature that shares the victim's name is still a creature: the
    // creature hook is only ever reached from the branch where the killer is
    // not a Player, so there is no self-damage hiding in it.
    CheckKind("a creature named like its victim is not self-inflicted",
              NameTheKiller(true, "creature", "Bork", "Bork"),
              KillerKind::Creature);

    // Nothing named a killer. This is the row the old comment believed every
    // fall produced, and it does still happen - a GM `.die`, a despawn, a
    // script that kills without an attacker.
    CheckKind("no hook fired at all", NameTheKiller(false, "", "", "Bork"),
              KillerKind::Unattributed);
    CheckKind("an absent hook ignores whatever is in the other fields",
              NameTheKiller(false, "player", "Bork", "Bork"),
              KillerKind::Unattributed);
}

// Both sides reach this from different places, so neither the case nor the
// surrounding whitespace of a name may decide a death's cause.
void TheComparisonIsOnTheNameAndNotOnItsSpelling()
{
    CheckKind("case does not make it a PvP kill",
              NameTheKiller(true, "player", "BORK", "bork"),
              KillerKind::SelfInflicted);
    CheckKind("a trailing space does not either",
              NameTheKiller(true, "PLAYER", "Bork ", " Bork"),
              KillerKind::SelfInflicted);
    CheckKind("and it still separates two different names",
              NameTheKiller(true, "player", " GRUG ", "bork"),
              KillerKind::Player);
}

// A blank on either side is not evidence of anything, and 'player' is the
// claim that costs the most when it is wrong.
void AnUnnameableVictimIsNotAPvPKill()
{
    CheckKind("no victim name to compare against",
              NameTheKiller(true, "player", "Bork", ""),
              KillerKind::Unattributed);
    CheckKind("a blank killer against a real victim is not self-inflicted",
              NameTheKiller(true, "player", "", "Bork"), KillerKind::Player);
    CheckKind("a hook type nobody recognises claims nothing",
              NameTheKiller(true, "gm", "Bork", "Bork"),
              KillerKind::Unattributed);
}

// The column keeps the value every existing reader groups by for the
// unattributed case, and only the one that used to hide inside 'player' is new.
void EveryValueHasItsOwnName()
{
    CheckName("unattributed", KillerKindName(KillerKind::Unattributed),
              "environment");
    CheckName("creature", KillerKindName(KillerKind::Creature), "creature");
    CheckName("player", KillerKindName(KillerKind::Player), "player");
    CheckName("self", KillerKindName(KillerKind::SelfInflicted), "self");
}

}  // namespace

int main()
{
    ADeathNamingItsOwnVictimIsSelfInflicted();
    ARealPlayerKillIsStillAPlayerKill();
    ACreatureKillAndAnAbsentHookAreTheirOwnAnswers();
    TheComparisonIsOnTheNameAndNotOnItsSpelling();
    AnUnnameableVictimIsNotAPvPKill();
    EveryValueHasItsOwnName();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a death that named its own victim says so, and is not a PvP kill\n");
    return EXIT_SUCCESS;
}
