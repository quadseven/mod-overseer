/*
 * What a stalled run records when the dungeon module switched itself off.
 *
 * This compiles against the pure decision file and nothing from AzerothCore.
 * The module's own stop text is the cause the row names; how the exit was
 * cleaned up is a separate event.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::DungeonModuleStopWords;

namespace
{

int failures = 0;

void Check(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s\n  got:  %s\n  want: %s\n", what, got.c_str(), want.c_str());
    ++failures;
}

void ALowManaWaitNamesTheMemberAndTheReason()
{
    Check("low mana",
          DungeonModuleStopWords("Dungeon clear stopped: the party never became ready after "
                                 "180s (Waiting on Uzza (low mana))"),
          "dungeon clear stopped: the party never became ready after 180s, waiting on Uzza "
          "(low mana)");
    Check("bare text",
          DungeonModuleStopWords("the party never became ready after 180s (Waiting on Bo "
                                 "(out of range))"),
          "dungeon clear stopped: the party never became ready after 180s, waiting on Bo "
          "(out of range)");
}

void AStopWithoutAMemberKeepsItsText()
{
    Check("no member", DungeonModuleStopWords("No bosses left - disabling"),
          "dungeon clear stopped: No bosses left - disabling");
}

void NoReasonIsSaidPlainly()
{
    Check("empty", DungeonModuleStopWords(""),
          "dungeon clear switched itself off; no reason given");
    Check("blank prefix only", DungeonModuleStopWords("Dungeon clear stopped:  "),
          "dungeon clear switched itself off; no reason given");
}

}  // namespace

int main()
{
    ALowManaWaitNamesTheMemberAndTheReason();
    AStopWithoutAMemberKeepsItsText();
    NoReasonIsSaidPlainly();
    if (failures == 0)
        std::printf("PASS: dungeon module stop words\n");
    return failures == 0 ? 0 : 1;
}
