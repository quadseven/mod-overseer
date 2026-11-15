/*
 * Why an ordered raid's FORM step does not seat a character (mod-overseer#1555).
 * A raid that formed 31 of 40 seats said nothing about the other nine, which
 * sat in dungeon-finder groups left over in the data. The skip reason is one
 * pure answer: it names every refused seat in the log, and a finder group the
 * finder no longer serves is a seat that can join once that group is cleared.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>

using OverseerDecisions::FinderState;
using OverseerDecisions::RaidSeatBlock;
using OverseerDecisions::RaidSeatJoinFacts;

namespace
{
int failures = 0;

void CheckBlock(char const* what, RaidSeatJoinFacts const& facts, RaidSeatBlock want)
{
    RaidSeatBlock const got = OverseerDecisions::RaidSeatBlockOf(facts);
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, OverseerDecisions::RaidSeatBlockWord(got),
                OverseerDecisions::RaidSeatBlockWord(want));
    ++failures;
}

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %d, wanted %d\n", what, got, want);
    ++failures;
}

RaidSeatJoinFacts Free()
{
    RaidSeatJoinFacts f;
    f.inWorld = true;
    return f;
}
}  // namespace

int main()
{
    CheckBlock("free seat", Free(), RaidSeatBlock::None);

    RaidSeatJoinFacts f = Free();
    f.inWorld = false;
    CheckBlock("not in the world", f, RaidSeatBlock::NotInWorld);

    f = Free();
    f.inThisRaid = true;
    CheckBlock("already seated", f, RaidSeatBlock::Seated);

    f = Free();
    f.raidFull = true;
    CheckBlock("raid full", f, RaidSeatBlock::RaidFull);

    f = Free();
    f.inBattleground = true;
    CheckBlock("battleground", f, RaidSeatBlock::Battleground);

    f = Free();
    f.inBattlefield = true;
    CheckBlock("battlefield", f, RaidSeatBlock::Battlefield);

    // A finder group still matching or with a member inside is the core's.
    f = Free();
    f.inFinderGroup = true;
    f.finderState = FinderState::Queued;
    CheckBlock("finder group, queued", f, RaidSeatBlock::FinderGroup);

    f = Free();
    f.inFinderGroup = true;
    f.finderState = FinderState::Dungeon;
    f.finderMemberInInstance = true;
    CheckBlock("finder group, a member inside", f, RaidSeatBlock::FinderGroup);

    // The leftover the report found: not matching, nobody inside. Joinable,
    // and the adapter clears the group first.
    f = Free();
    f.inFinderGroup = true;
    f.finderState = FinderState::Other;
    CheckBlock("stale finder group is joinable", f, RaidSeatBlock::None);
    CheckBool("stale finder group needs a clear", OverseerDecisions::RaidSeatNeedsFinderClear(f),
              true);

    f.finderState = FinderState::None;
    CheckBlock("finder group with state none is joinable", f, RaidSeatBlock::None);

    f = Free();
    CheckBool("free seat needs no clear", OverseerDecisions::RaidSeatNeedsFinderClear(f), false);
    f.inFinderGroup = true;
    f.finderState = FinderState::Proposal;
    CheckBool("live finder group is not cleared", OverseerDecisions::RaidSeatNeedsFinderClear(f),
              false);

    // The order of the reasons: a character out of the world is that first.
    f = Free();
    f.inWorld = false;
    f.inFinderGroup = true;
    CheckBlock("out of the world beats the group", f, RaidSeatBlock::NotInWorld);
    f = Free();
    f.raidFull = true;
    f.inFinderGroup = true;
    f.finderState = FinderState::Queued;
    CheckBlock("full raid beats a finder group", f, RaidSeatBlock::RaidFull);

    // Every reason has its own word for the log, none empty.
    RaidSeatBlock const all[] = {RaidSeatBlock::NotInWorld, RaidSeatBlock::Seated,
                                 RaidSeatBlock::RaidFull,   RaidSeatBlock::Battleground,
                                 RaidSeatBlock::Battlefield, RaidSeatBlock::FinderGroup};
    for (RaidSeatBlock const b : all)
        CheckBool("reason has a word", std::strlen(OverseerDecisions::RaidSeatBlockWord(b)) > 0,
                  true);
    CheckBool("finder reason word",
              std::strcmp(OverseerDecisions::RaidSeatBlockWord(RaidSeatBlock::FinderGroup),
                          "in a dungeon-finder group the finder still serves") == 0,
              true);

    // The log line: seats named with reasons, the count short, once.
    std::vector<OverseerDecisions::RaidSeatSkip> skips = {
        {"Alpha", RaidSeatBlock::FinderGroup}, {"Bravo", RaidSeatBlock::NotInWorld}};
    std::string const line = OverseerDecisions::RaidSeatSkipLine(skips, 40, 31);
    CheckBool("line names the seat and reason",
              line.find("Alpha (in a dungeon-finder group the finder still serves)") !=
                  std::string::npos,
              true);
    CheckBool("line names the second seat", line.find("Bravo (not in the world)") !=
                                                std::string::npos, true);
    CheckBool("line carries the count short",
              line.find("31 of 40 seats") != std::string::npos, true);
    CheckBool("nothing skipped says nothing",
              OverseerDecisions::RaidSeatSkipLine({}, 40, 40).empty(), true);

    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
