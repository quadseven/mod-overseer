/*
 * A town trip does not write a counter aim the travel drive has called off.
 *
 * The wedge: a family stands together in a city and its leader's walk to a
 * vendor makes no progress, so the travel drive releases it and refuses the aim
 * for a cool-off. The town trip stayed open and wrote the same aim again on
 * every five second poll; the drive cleared it again at once, so the leader
 * claimed and lost the travel column again and again and the family was given
 * nothing else to do for the whole life of the trip.
 *
 * This test polls the claim rule the way the trip does, every five seconds
 * across a refusal, and counts the writes: the old behaviour (always claim)
 * writes on every poll, the committed rule writes none until the refusal ends.
 * It also reads the adapter to prove the trip asks the rule both when it opens
 * and when it is about to walk a member.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else; run from the
 * repository root.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::ReadTownTripAimClaim;
using OverseerDecisions::TownTripAimClaim;

namespace
{
int failures = 0;

void Check(char const* what, bool holds)
{
    if (holds)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

// Writes the trip makes over `pollsOf5s` polls when the aim was refused at
// time zero for `refusalSeconds`. `old` is the behaviour before the rule.
unsigned Writes(unsigned pollsOf5s, std::int64_t refusalSeconds, bool old)
{
    unsigned writes = 0;
    for (unsigned poll = 0; poll < pollsOf5s; ++poll)
    {
        std::int64_t const sinceRefused = std::int64_t(poll) * 5;
        if (old || ReadTownTripAimClaim(sinceRefused, refusalSeconds) == TownTripAimClaim::Claim)
            ++writes;
    }
    return writes;
}
} // namespace

int main()
{
    Check("a never-refused aim is claimed", ReadTownTripAimClaim(-1, 900) == TownTripAimClaim::Claim);
    Check("a refusal just made stands", ReadTownTripAimClaim(0, 900) == TownTripAimClaim::StandBack);
    Check("a refusal stands to the last second",
          ReadTownTripAimClaim(899, 900) == TownTripAimClaim::StandBack);
    Check("a refusal that has run its length is over",
          ReadTownTripAimClaim(900, 900) == TownTripAimClaim::Claim);
    Check("a zero-length refusal never stands", ReadTownTripAimClaim(0, 0) == TownTripAimClaim::Claim);

    // Ten minutes of polls inside a fifteen minute refusal.
    Check("before the rule the trip wrote the aim on every poll", Writes(120, 900, true) == 120);
    Check("with the rule the trip writes nothing inside the refusal", Writes(120, 900, false) == 0);
    // Twenty minutes: the writes resume only once the refusal has ended.
    Check("with the rule the trip writes again only after the refusal ends",
          Writes(240, 900, false) == 60);

    std::ifstream in("src/mod_overseer.cpp");
    Check("the adapter is readable from the repository root", bool(in));
    std::string const text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto const has = [&](char const* needle) { return text.find(needle) != std::string::npos; };
    Check("the adapter reads the rule", has("OverseerDecisions::ReadTownTripAimClaim("));
    Check("the trip asks before it opens", has("TownTripAimCalledOff(leaderName, aim, refusedBecause)"));
    Check("the trip asks before it walks a member",
          has("TownTripAimCalledOff(name, aim, refusedBecause)"));
    Check("the leader's refusal ends the whole trip",
          has("EndTownTrip(trip, members, \"the travel drive called the aim off\")"));

    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
