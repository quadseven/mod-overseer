/*
 * A party split across two copies of the same dungeon (#620).
 *
 * Measured on the dev realm after a worldserver restart: the four bots logged
 * in inside Ragefire Chasm in one instance, the head's client reconnected into
 * another. #628 stopped counting such a member as inside; nothing moved it,
 * and the run waited for its split_failed backstop. These pin that a member in
 * another copy is named as such and is walked out to come back in through the
 * door, and that the stranded walk (which aims at the outdoor door) leaves it
 * alone.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::DungeonRunAllThrough;
using OverseerDecisions::DungeonRunEntryBlockers;
using OverseerDecisions::DungeonRunEntryState;
using OverseerDecisions::DungeonRunOtherCopy;
using OverseerDecisions::DungeonRunWrongSide;

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

DungeonRunEntryState Member(char const* name, bool through, bool otherCopy, bool alive = true)
{
    DungeonRunEntryState state;
    state.name = name;
    state.seen = true;
    state.alive = alive;
    state.through = through;
    state.otherCopy = otherCopy;
    return state;
}

void AMemberInAnotherCopyIsNotInsideAndIsWalkedOut()
{
    std::vector<DungeonRunEntryState> const party = {
        Member("Zug", true, false),
        Member("Oz", false, true),
        Member("Uzza", false, true),
        Member("Zork", false, true),
        Member("Zrog", false, true, false),   // dead in the other copy
    };

    Check("the party is not all through", !DungeonRunAllThrough(party));

    std::vector<std::string> const walk = DungeonRunOtherCopy(party);
    Check("the living members are walked out",
          walk == std::vector<std::string>{"Oz", "Uzza", "Zork"});

    // Not also walked IN by the stranded walk, which aims at the outdoor door
    // and would pull against the walk out.
    Check("the stranded walk leaves them alone",
          DungeonRunWrongSide(party).walk.empty());

    std::string const blockers = DungeonRunEntryBlockers(party, 10.f);
    Check("the blockers line names the copy",
          blockers.find("Oz (in another copy of the dungeon)") != std::string::npos);
}

}  // namespace

int main()
{
    AMemberInAnotherCopyIsNotInsideAndIsWalkedOut();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("dungeon other copy: all passed\n");
    return 0;
}
