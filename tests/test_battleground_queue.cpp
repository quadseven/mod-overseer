/*
 * A member queues a battleground the way a player does (wow-overseer#589).
 *
 * WHAT IS PINNED HERE:
 *
 *   - The row is `bg-queue <av|wsg|ab>` on kind='guild', routed on its first
 *     word as the finder run is, so it never reaches DoGuild's own verbs and
 *     needs no ENUM migration. The three keys map to the core's
 *     BattlegroundTypeId (1 AV, 2 WSG, 3 AB); anything else is malformed.
 *   - Already inside a battleground, or already in this one's queue, is not
 *     a failure: the row says so and is delivered, so the bridge reads it as
 *     "waiting for the battle".
 *   - The refusals come in the core handler's own order, and a member that
 *     follows a group is refused: its leader queues the group.
 *   - Whether a refusal is worth asking again comes from its literal alone.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::BattlegroundQueueAlready;
using OverseerDecisions::BattlegroundQueueFacts;
using OverseerDecisions::BattlegroundQueueRefusal;
using OverseerDecisions::BattlegroundQueueRequest;
using OverseerDecisions::BattlegroundQueueRetry;
using OverseerDecisions::IsBattlegroundQueueRow;
using OverseerDecisions::IsGuildFinderRow;
using OverseerDecisions::ParseBattlegroundQueueRequest;
using OverseerDecisions::TownRetry;

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

void CheckNumber(char const* what, long long got, long long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %lld, wanted %lld\n", what, got, want);
    ++failures;
}

void CheckWord(char const* what, char const* got, char const* want)
{
    std::string const have = got ? got : "(none)";
    std::string const wanted = want ? want : "(none)";
    if (have == wanted)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, have.c_str(), wanted.c_str());
    ++failures;
}

void TheRowIsRoutedOnItsFirstWord()
{
    Check("a bg-queue row is this verb", IsBattlegroundQueueRow("bg-queue av"), true);
    Check("with blanks around it", IsBattlegroundQueueRow("  bg-queue   wsg "), true);
    Check("a malformed one still reaches the parser", IsBattlegroundQueueRow("bg-queue"), true);
    Check("a DoGuild verb is not", IsBattlegroundQueueRow("invite Auren"), false);
    Check("nor the finder run", IsBattlegroundQueueRow("finder-run deadmines A B C D"), false);
    Check("nor an empty row", IsBattlegroundQueueRow(""), false);
    Check("and the finder run does not take it", IsGuildFinderRow("bg-queue av"), false);

    // THE TWO kind='guild' ROUTES ARE DISJOINT, so the dispatcher's order
    // between them cannot matter: no row is claimed by both.
    for (char const* row : {"bg-queue av", "bg-queue wsg", "bg-queue ab", "bg-queue",
                            "finder-run deadmines Ann Bo Cy Di", "finder-run", "invite Auren"})
        Check(row, IsBattlegroundQueueRow(row) && IsGuildFinderRow(row), false);
}

void EachBattlegroundIsTheCoresType()
{
    struct
    {
        char const* command;
        char const* key;
        long long type;
    } const CASES[] = {
        {"bg-queue av", "av", 1},
        {"bg-queue wsg", "wsg", 2},
        {"bg-queue ab", "ab", 3},
    };
    for (auto const& c : CASES)
    {
        BattlegroundQueueRequest const request = ParseBattlegroundQueueRequest(c.command);
        Check(c.command, request.valid, true);
        CheckWord("the key", request.key.c_str(), c.key);
        CheckNumber("the BattlegroundTypeId", request.bgTypeId, c.type);
    }
}

void EverythingElseIsMalformed()
{
    CheckWord("no battleground", ParseBattlegroundQueueRequest("bg-queue").error.c_str(),
              "malformed bg-queue: want exactly one battleground (av, wsg or ab)");
    CheckWord("two battlegrounds",
              ParseBattlegroundQueueRequest("bg-queue av ab").error.c_str(),
              "malformed bg-queue: want exactly one battleground (av, wsg or ab)");
    CheckWord("an arena is not a battleground",
              ParseBattlegroundQueueRequest("bg-queue 2v2").error.c_str(),
              "malformed bg-queue: unknown battleground (want av, wsg or ab)");
    CheckWord("upper case is not a key",
              ParseBattlegroundQueueRequest("bg-queue AV").error.c_str(),
              "malformed bg-queue: unknown battleground (want av, wsg or ab)");
    CheckWord("another verb", ParseBattlegroundQueueRequest("queue av").error.c_str(),
              "malformed bg-queue: want bg-queue <av|wsg|ab>");
    Check("and none of them is valid", ParseBattlegroundQueueRequest("bg-queue 2v2").valid,
          false);
}

void BeingThereAlreadyIsNotAFailure()
{
    BattlegroundQueueFacts inside;
    inside.inBattleground = true;
    CheckWord("inside a battleground", BattlegroundQueueAlready(inside),
              "already in a battleground");
    BattlegroundQueueFacts queued;
    queued.queuedForThis = true;
    CheckWord("in this queue", BattlegroundQueueAlready(queued),
              "already in this battleground's queue");
    CheckWord("neither", BattlegroundQueueAlready(BattlegroundQueueFacts{}), nullptr);
    CheckWord("and a fresh member is no refusal either",
              BattlegroundQueueRefusal(BattlegroundQueueFacts{}), nullptr);
}

void TheRefusalsComeInTheHandlersOrder()
{
    BattlegroundQueueFacts facts;
    facts.bracketFits = false;
    facts.freeQueueSlot = false;
    facts.deserter = true;
    CheckWord("a level no bracket holds is named first", BattlegroundQueueRefusal(facts),
              "no bracket of that battleground holds this level");
    facts.bracketFits = true;
    CheckWord("then a full queue list", BattlegroundQueueRefusal(facts),
              "no free battleground queue slot");
    facts.freeQueueSlot = true;
    CheckWord("then the deserter debuff", BattlegroundQueueRefusal(facts), "deserter");

    BattlegroundQueueFacts follower;
    follower.groupFollower = true;
    CheckWord("a group follower waits for its leader", BattlegroundQueueRefusal(follower),
              "a group member is queued by its leader");
    BattlegroundQueueFacts finder;
    finder.inDungeonFinder = true;
    CheckWord("the dungeon finder", BattlegroundQueueRefusal(finder), "in the dungeon finder");
    BattlegroundQueueFacts level;
    level.levelAllowed = false;
    CheckWord("a level the battleground turns away", BattlegroundQueueRefusal(level),
              "level is not allowed into that battleground");
}

void TheLevelAndTheGrammarNeverChangeByWaiting()
{
    for (char const* detail :
         {"malformed bg-queue request", "no such battleground",
          "no bracket of that battleground holds this level",
          "level is not allowed into that battleground",
          "a group member is queued by its leader"})
        Check(detail, BattlegroundQueueRetry(detail) == TownRetry::Never, true);
    for (char const* detail : {"no free battleground queue slot", "deserter",
                               "in the dungeon finder", "the core did not queue the character",
                               "something nobody wrote down"})
        Check(detail, BattlegroundQueueRetry(detail) == TownRetry::Later, true);
}

}  // namespace

int main()
{
    TheRowIsRoutedOnItsFirstWord();
    EachBattlegroundIsTheCoresType();
    EverythingElseIsMalformed();
    BeingThereAlreadyIsNotAFailure();
    TheRefusalsComeInTheHandlersOrder();
    TheLevelAndTheGrammarNeverChangeByWaiting();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a member queues a battleground as a player does\n");
    return EXIT_SUCCESS;
}
