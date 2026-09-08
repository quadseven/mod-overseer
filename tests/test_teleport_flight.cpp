/*
 * A far teleport that is still in flight is the middle of a success, and this
 * file exists because a read-back called it a failure.
 *
 * mod-overseer#310. The first real cross-continent hearth this family ever cast
 * WORKED. The character left Elwynn Forest on map 0 and arrived on map 1. The
 * row said:
 *
 *   status: error
 *   detail: left the world before the hearth could be read back
 *   result: {"outcome":"unreadable","casting_after_call":true,
 *            "teleport_still_in_flight":null,"now":null}
 *
 * Two things went wrong and they are the same thing. Player::TeleportTo on a
 * map change calls RemoveFromWorld, so IsInWorld() is false for the length of
 * the crossing. ObjectAccessor::FindPlayerByName takes `bool checkInWorld` and
 * DEFAULTS IT TO TRUE, so the lookup returned null - and the verdict logic read
 * null as "logged out" and wrote `error` about a character that was, at that
 * moment, mid-ocean and about to land exactly where it was asked to.
 *
 * Leaving the world IS what a successful cross-map teleport looks like from the
 * world thread. So the rules pinned here separate the four readings a
 * read-back can get, and the one that matters is that a crossing in progress is
 * neither an arrival nor a departure: it is "ask again".
 *
 * Shared, not hearth-shaped, because kind='summon' (#313) ends in the same far
 * teleport and would otherwise rediscover this the same expensive way.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::ReadTeleportFlight;
using OverseerDecisions::TeleportFlight;
using OverseerDecisions::TeleportFlightWord;

namespace
{

int failures = 0;

void CheckFlight(char const* what, TeleportFlight got, TeleportFlight want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, TeleportFlightWord(got),
                TeleportFlightWord(want));
    ++failures;
}

void CheckWord(char const* what, char const* got, char const* want)
{
    if (std::string(got) == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got, want);
    ++failures;
}

// The ceiling mod_overseer.cpp passes in is a window plus a settle allowance.
// Named here so a failure reads as the rule rather than as a number.
constexpr uint32_t CEILING_MS = 25000;

// THE ROW THAT PAID FOR THIS FILE. A character mid far-teleport: still in the
// name map, out of the world, semaphore set. Every one of those three is what a
// working crossing looks like.
void TheCrossingThatWasCalledAFailure()
{
    CheckFlight("mid-crossing, well inside the ceiling",
                ReadTeleportFlight(true, false, true, 6000, CEILING_MS),
                TeleportFlight::InFlight);
    CheckFlight("mid-crossing, one millisecond inside the ceiling",
                ReadTeleportFlight(true, false, true, CEILING_MS - 1, CEILING_MS),
                TeleportFlight::InFlight);
}

// A crossing cannot be waited on for ever, or a teleport that never lands holds
// a row in `verifying` until the claim lease reaps it and says nothing useful.
// Past the ceiling the reading is taken anyway - and named apart, so the row can
// say the position it was judged on was read during a teleport.
void ACrossingThatNeverLandsIsStillAnswered()
{
    CheckFlight("exactly at the ceiling is no longer waiting",
                ReadTeleportFlight(true, false, true, CEILING_MS, CEILING_MS),
                TeleportFlight::Stranded);
    CheckFlight("long past the ceiling",
                ReadTeleportFlight(true, false, true, 600000, CEILING_MS),
                TeleportFlight::Stranded);
    CheckFlight("a zero ceiling never waits",
                ReadTeleportFlight(true, false, true, 0, 0),
                TeleportFlight::Stranded);
}

// A NEAR teleport keeps the character in the world and still sets a semaphore.
// It is a crossing too as far as a read-back is concerned: the position on the
// far side of it is not the position the character is about to be at.
void ANearTeleportIsAlsoInFlight()
{
    CheckFlight("in the world and still teleporting",
                ReadTeleportFlight(true, true, true, 3000, CEILING_MS),
                TeleportFlight::InFlight);
}

// The only reading that means what the hearth's message said. Out of the name
// map is out of the game; nothing survives it and there is nothing to wait for.
void OnlyLeavingTheNameMapIsLeaving()
{
    CheckFlight("not in the name map at all",
                ReadTeleportFlight(false, false, false, 6000, CEILING_MS),
                TeleportFlight::Gone);
    // AND THE ORDER MATTERS. A logout does not become a crossing because a
    // semaphore happened to be set on the way out; the name map is asked first
    // and it is the only thing that answers this question.
    CheckFlight("gone outranks a semaphore",
                ReadTeleportFlight(false, false, true, 6000, CEILING_MS),
                TeleportFlight::Gone);
    CheckFlight("gone outranks being past the ceiling",
                ReadTeleportFlight(false, true, true, 600000, CEILING_MS),
                TeleportFlight::Gone);
}

// In the name map, not teleporting, and not in the world: a logout that has
// begun but has not finished clearing. There is no crossing to wait out, so
// this is Gone and not InFlight - which is the one case where the old code's
// answer happened to be right, for the wrong reason.
void ALogoutInProgressIsGoneAndNotACrossing()
{
    CheckFlight("named, not teleporting, out of the world",
                ReadTeleportFlight(true, false, false, 6000, CEILING_MS),
                TeleportFlight::Gone);
}

// Nothing in flight, in the world, in the map. The ordinary reading, and the
// only one a verdict may be built on.
void StandingStillIsTheOnlyThingWorthReading()
{
    CheckFlight("in the world, nothing in flight",
                ReadTeleportFlight(true, true, false, 6000, CEILING_MS),
                TeleportFlight::Landed);
    CheckFlight("waiting time does not turn a landing into anything else",
                ReadTeleportFlight(true, true, false, 600000, CEILING_MS),
                TeleportFlight::Landed);
}

void TheFlightWordsAreTheOnesARowCarries()
{
    CheckWord("landed", TeleportFlightWord(TeleportFlight::Landed), "landed");
    CheckWord("in-flight", TeleportFlightWord(TeleportFlight::InFlight), "in-flight");
    CheckWord("stranded", TeleportFlightWord(TeleportFlight::Stranded), "stranded");
    CheckWord("gone", TeleportFlightWord(TeleportFlight::Gone), "gone");
}

}  // namespace

int main()
{
    TheCrossingThatWasCalledAFailure();
    ACrossingThatNeverLandsIsStillAnswered();
    ANearTeleportIsAlsoInFlight();
    OnlyLeavingTheNameMapIsLeaving();
    ALogoutInProgressIsGoneAndNotACrossing();
    StandingStillIsTheOnlyThingWorthReading();
    TheFlightWordsAreTheOnesARowCarries();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a teleport in flight is the middle of a success, and only the name "
                "map says a character left\n");
    return EXIT_SUCCESS;
}
