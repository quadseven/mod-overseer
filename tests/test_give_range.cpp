/*
 * The give's range rule (#566), decided without a world.
 *
 * A kind='give' row used to move an item between two online characters at any
 * distance. It now keeps the rule a trade keeps: the two stand within
 * TRADE_DISTANCE (11.11 yards, ObjectDefines.h) of each other, on one map,
 * measured the way HandleInitiateTradeOpcode measures it. Pinned here:
 *
 *   - Two characters standing together may give.
 *   - Exactly at, and past, the trade distance may not; the core's own compare
 *     is strict.
 *   - Two maps may not, whatever the numbers say.
 *   - The refusal names the two ways that work at a distance, and carries no
 *     quote character, because `detail` is written straight into SQL.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::GiveRangeRefusalFor;
namespace R = OverseerDecisions::GiveRangeRefusal;

namespace
{

int failures = 0;

// The core's TRADE_DISTANCE, spelled here only as the argument the adapter
// passes; the decision itself carries no copy of it.
constexpr float TRADE = 11.11f;

void CheckText(char const* what, std::string const& got, char const* want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: '%s', wanted '%s'\n", what, got.c_str(), want);
    ++failures;
}

void TwoCharactersTogetherMayGive()
{
    CheckText("face to face", GiveRangeRefusalFor(true, 0.f, TRADE), "");
    CheckText("a few yards", GiveRangeRefusalFor(true, 4.5f, TRADE), "");
    CheckText("just inside", GiveRangeRefusalFor(true, 11.1f, TRADE), "");
}

void PastTradeRangeIsRefused()
{
    CheckText("exactly at the distance, as the core compares",
              GiveRangeRefusalFor(true, TRADE, TRADE), R::TooFar);
    CheckText("just outside", GiveRangeRefusalFor(true, 11.2f, TRADE), R::TooFar);
    CheckText("the family bag hand-over at a thousand yards",
              GiveRangeRefusalFor(true, 1000.f, TRADE), R::TooFar);
    CheckText("a reading that is not a number",
              GiveRangeRefusalFor(true, std::nanf(""), TRADE), R::TooFar);
}

void AnotherMapIsRefused()
{
    // Silithus to Winterspring is one map; a continent apart is not.
    CheckText("another map, even at a small reading",
              GiveRangeRefusalFor(false, 2.f, TRADE), R::OtherMap);
    CheckText("another map, no reading", GiveRangeRefusalFor(false, -1.f, TRADE), R::OtherMap);
}

void TheRefusalSaysWhatToDoAndIsSafeInSql()
{
    for (char const* reason : {R::TooFar, R::OtherMap})
    {
        std::string const text = reason;
        if (text.find("meet within trade range") == std::string::npos
            || text.find("mail") == std::string::npos)
        {
            std::printf("FAIL '%s' does not name meeting and mail\n", reason);
            ++failures;
        }
        if (text.find('\'') != std::string::npos || text.find('"') != std::string::npos)
        {
            std::printf("FAIL '%s' carries a quote character\n", reason);
            ++failures;
        }
    }
}

}  // namespace

int main()
{
    TwoCharactersTogetherMayGive();
    PastTradeRangeIsRefused();
    AnotherMapIsRefused();
    TheRefusalSaysWhatToDoAndIsSafeInSql();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the give range decisions hold\n");
    return EXIT_SUCCESS;
}
