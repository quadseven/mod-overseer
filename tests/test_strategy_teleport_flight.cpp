/*
 * The strategy read-back must not read a teleport in flight as a departure.
 *
 * mod-overseer#319, the one read-back #310 was not carried to. A strategy
 * command teleports nobody, but mod-playerbots moves bots on its own
 * initiative, and a far teleport takes a character OUT OF THE WORLD for the
 * whole crossing. ResolveStrategyChecks looked the character up with
 * ObjectAccessor::FindPlayerByName(name), whose `checkInWorld` defaults to
 * true, so a bot mid-crossing came back null and its row was written
 *
 *   status: error
 *   detail: left the world before the change could be read back
 *
 * about a character that was still logged in and about to land.
 *
 * The rule itself is OverseerDecisions::ReadTeleportFlight, pinned by
 * test_teleport_flight.cpp. What this file pins is the call site: the adapter
 * cannot be compiled here (it needs a core), so its source is read instead, the
 * same way test_dungeon_exit.cpp and test_crossing_gives_up.cpp read it.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else; run from the
 * repository root, as check.decisions.yml runs it.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::ReadTeleportFlight;
using OverseerDecisions::TeleportFlight;
using OverseerDecisions::TeleportFlightWord;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

bool Has(std::string const& body, char const* needle)
{
    return body.find(needle) != std::string::npos;
}

std::string ResolveStrategyChecksBody()
{
    std::ifstream input("src/mod_overseer.cpp");
    Check("the adapter source is readable from the repository root", input.good());
    std::string const source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    std::size_t const start = source.find("void ResolveStrategyChecks(uint32 elapsedMs)");
    Check("ResolveStrategyChecks is still where the strategy read-back lives",
          start != std::string::npos);
    if (start == std::string::npos)
        return std::string();
    // The function is a member at four spaces; its closing brace is the first
    // line that is exactly four spaces and a brace.
    std::size_t const end = source.find("\n    }\n", start);
    Check("ResolveStrategyChecks has a closing brace", end != std::string::npos);
    if (end == std::string::npos)
        return std::string();
    return source.substr(start, end - start);
}

// THE LOOKUP. With checkInWorld defaulted to true, a crossing reads as null.
void TheLookupKeepsACharacterThatIsCrossing(std::string const& body)
{
    Check("the strategy read-back asks the name map with checkInWorld false",
          Has(body, "ObjectAccessor::FindPlayerByName(check.targetName, false)"));
    Check("the strategy read-back no longer asks with checkInWorld defaulted to true",
          !Has(body, "ObjectAccessor::FindPlayerByName(check.targetName)"));
}

// THE READING. The shared rule, not a hand-rolled IsInWorld test.
void TheReadingIsTheSharedRule(std::string const& body)
{
    Check("the strategy read-back reads the flight through ReadTeleportFlight",
          Has(body, "ReadTeleportFlight(bot != nullptr, bot && bot->IsInWorld(),") &&
              Has(body, "bot && bot->IsBeingTeleported(), check.waitedMs"));

    std::size_t const gone = body.find("flight == TeleportFlight::Gone");
    std::size_t const error = body.find("left the world before the change could be read back");
    Check("only Gone is written as a departure",
          gone != std::string::npos && error != std::string::npos && gone < error);

    // InFlight is "ask again": the check goes back on the waiting list BEFORE
    // any strategy is read off an engine that is mid-crossing.
    std::size_t const inFlight = body.find("flight == TeleportFlight::InFlight");
    std::size_t const requeue =
        inFlight == std::string::npos ? std::string::npos
                                      : body.find("stillWaiting.push_back(check);", inFlight);
    std::size_t const firstRead = body.find("StrategyPresent(botAI, item)");
    Check("a check whose bot is in flight is asked again rather than judged",
          inFlight != std::string::npos && requeue != std::string::npos &&
              firstRead != std::string::npos && requeue < firstRead);

    // The row says what the verdict was judged on, so a Stranded reading is
    // visible after the fact rather than indistinguishable from Landed.
    Check("the result names the flight it was judged on",
          Has(body, "\"flight\\\":\" << J(TeleportFlightWord(flight))"));
}

// THE CEILING. The strategy read-back passes its own grace plus the hearth's
// settle allowance; with that ceiling a bot crossing at the end of the grace
// is still InFlight, and one that never lands is judged rather than waited on.
void TheCeilingBoundsTheWait(std::string const& body)
{
    Check("the ceiling is the grace plus the settle allowance",
          Has(body, "VERIFY_GRACE_MS + HEARTH_SETTLE_CEILING_MS"));

    constexpr uint32_t GRACE_MS = 6000;    // VERIFY_GRACE_MS in mod_overseer.cpp
    constexpr uint32_t SETTLE_MS = 10000;  // HEARTH_SETTLE_CEILING_MS
    constexpr uint32_t CEILING_MS = GRACE_MS + SETTLE_MS;
    TeleportFlight const atGrace = ReadTeleportFlight(true, false, true, GRACE_MS, CEILING_MS);
    Check("a bot crossing when the grace runs out is still in flight",
          atGrace == TeleportFlight::InFlight);
    TeleportFlight const pastCeiling =
        ReadTeleportFlight(true, false, true, CEILING_MS, CEILING_MS);
    Check("a bot still crossing at the ceiling is judged as stranded",
          pastCeiling == TeleportFlight::Stranded);
    Check("the flight word is the one the row will carry",
          std::string(TeleportFlightWord(TeleportFlight::InFlight)) == "in-flight");
}

} // namespace

int main()
{
    std::string const body = ResolveStrategyChecksBody();
    TheLookupKeepsACharacterThatIsCrossing(body);
    TheReadingIsTheSharedRule(body);
    TheCeilingBoundsTheWait(body);

    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_strategy_teleport_flight: all checks passed\n");
    return 0;
}
