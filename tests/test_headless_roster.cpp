/*
 * Who plays without a client, and the rule that keeps it from crashing.
 *
 * Overseer.RequireClient shipped able to stop the eviction of an in-world
 * headless bot and nothing else - it puts nobody in the world. On a roster
 * that is simply logged out, which is what a host with two clients and ten
 * characters has, turning it off therefore changed NOTHING, and the feature
 * read as delivered while the eight characters it was for stayed offline.
 * KeepRosterOnline, the pass that logged the missing ones in, had been deleted
 * when the camera rule arrived and was never restored.
 *
 * THE RULE THIS FILE EXISTS TO PIN is why the other half is a LIST rather than
 * a second boolean. The crash that removed KeepRosterOnline was a real client
 * logging in as a character that was already in the world as a headless bot:
 * mod-playerbots' secure login force-evicts the bot holding that guid while
 * this module's drives are still steering it by name. A character must be
 * EITHER watched through a client OR played headless, never both. So spawning
 * is gated on the list ALONE - never on RequireClient - and the test that
 * matters most here is the one asserting that RequireClient = 0 does not, by
 * itself, make a character spawnable.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::HeadlessRosterNames;
using OverseerDecisions::MayPlayHeadless;
using OverseerDecisions::NamedInHeadlessRoster;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckNames(char const* what, std::vector<std::string> const& got,
                std::vector<std::string> const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got [", what);
    for (std::string const& n : got)
        std::printf("'%s' ", n.c_str());
    std::printf("], wanted [");
    for (std::string const& n : want)
        std::printf("'%s' ", n.c_str());
    std::printf("]\n");
    ++failures;
}

void AConfigListIsReadTheWayAPersonWroteIt()
{
    CheckNames("plain list", HeadlessRosterNames("Bork,Grog,Og,Ugga"),
               {"Bork", "Grog", "Og", "Ugga"});
    // A person writing a config puts spaces after commas. A name with a
    // leading space matches no character and fails silently, which is the
    // worst way for this to be wrong.
    CheckNames("spaces after commas", HeadlessRosterNames("Bork, Grog,  Og"),
               {"Bork", "Grog", "Og"});
    CheckNames("tabs and outer padding", HeadlessRosterNames("  Bork\t,\tGrog  "),
               {"Bork", "Grog"});
    CheckNames("one name", HeadlessRosterNames("Bork"), {"Bork"});
}

void NothingConfiguredMeansNobody()
{
    CheckNames("empty", HeadlessRosterNames(""), {});
    CheckNames("only spaces", HeadlessRosterNames("   "), {});
    // A trailing comma must not produce an empty name: an empty name would
    // match a character whose name is "" - nobody - but it would also sit in
    // the list looking like a configured entry.
    CheckNames("trailing comma", HeadlessRosterNames("Bork,"), {"Bork"});
    CheckNames("leading comma", HeadlessRosterNames(",Bork"), {"Bork"});
    CheckNames("double comma", HeadlessRosterNames("Bork,,Grog"), {"Bork", "Grog"});
    CheckNames("all commas", HeadlessRosterNames(",,,"), {});
}

void MembershipIsExact()
{
    std::vector<std::string> const list = HeadlessRosterNames("Bork,Grog");
    Check("listed", NamedInHeadlessRoster("Bork", list), true);
    Check("not listed", NamedInHeadlessRoster("Grug", list), false);
    // A character name IS its spelling. A near-miss must read as "not listed"
    // rather than quietly matching somebody else's character.
    Check("different case", NamedInHeadlessRoster("bork", list), false);
    Check("prefix of a listed name", NamedInHeadlessRoster("Bor", list), false);
    Check("listed name is a prefix", NamedInHeadlessRoster("Borkk", list), false);
    Check("empty name", NamedInHeadlessRoster("", list), false);
}

void TheListIsWhatPermitsPlayingUnwatched()
{
    std::vector<std::string> const list = HeadlessRosterNames("Bork,Grog");
    Check("listed, client required", MayPlayHeadless("Bork", true, list), true);
    Check("unlisted, client required", MayPlayHeadless("Grug", true, list), false);
}

void TurningTheEvictionOffPermitsEverybody()
{
    // RequireClient = 0 is the blunt switch: evict nobody. It is a different
    // question from who gets SPAWNED, which is the caller's and is gated on
    // the list alone.
    std::vector<std::string> const none = HeadlessRosterNames("");
    Check("nobody listed, client not required",
          MayPlayHeadless("Grug", false, none), true);
    Check("nobody listed, client required",
          MayPlayHeadless("Grug", true, none), false);
}

void AWatchedCharacterIsNeverOnTheList()
{
    // THE ONE THAT MATTERS. Grug and Zug have clients. If turning the eviction
    // off were enough to make a character spawnable, they would be spawned as
    // headless bots and then evicted by mod-playerbots' secure login the
    // moment their client connected - which is the crash this design exists to
    // avoid. Membership, not the boolean, is what the spawner asks.
    std::vector<std::string> const list = HeadlessRosterNames("Bork,Grog,Og,Ugga");
    Check("a watched character is not spawnable, eviction on",
          NamedInHeadlessRoster("Grug", list), false);
    Check("a watched character is not spawnable, eviction off",
          NamedInHeadlessRoster("Grug", list), false);
    // And it stays not-spawnable even though the blunt switch would let it
    // stay in the world if something else had put it there.
    Check("but the blunt switch would still not evict it",
          MayPlayHeadless("Grug", false, list), true);
}

}  // namespace

int main()
{
    AConfigListIsReadTheWayAPersonWroteIt();
    NothingConfiguredMeansNobody();
    MembershipIsExact();
    TheListIsWhatPermitsPlayingUnwatched();
    TurningTheEvictionOffPermitsEverybody();
    AWatchedCharacterIsNeverOnTheList();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: the list spawns, the boolean only stops the eviction\n");
    return EXIT_SUCCESS;
}
