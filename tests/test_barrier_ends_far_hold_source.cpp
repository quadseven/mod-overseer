/*
 * BARRIER ends a member's too-far-to-walk hold before it walks that member.
 *
 * A follower past CATCH_UP_FOOT_LIMIT_YARDS is held where it stands under the
 * "wait for leader" verb until its leader comes back for it. A leader on a
 * dungeon run's staging walk never does: the run walks him to the door, and at
 * BARRIER the run walks the family there instead. That walk was handed to a
 * member the far hold was still keeping still, so the travel drive saw a held
 * character, the hold took the mover back every poll, and the member stood
 * where it was until the hold ran out its 15 minutes. Measured live on the dev
 * realm: three barriers in one morning closed with the family thousands of
 * yards out, each member flying toward the door the poll its hold expired.
 *
 * The hearth regroup already ends the far hold before it walks a member to the
 * family's inn. This pins the same order at BARRIER: the hold ends, then the
 * run's escort claims the walk.
 */

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

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

std::string ReadSource()
{
    std::ifstream file("src/mod_overseer.cpp");
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

std::string Between(std::string const& source, std::string const& start,
                    std::string const& end)
{
    std::string::size_type first = source.find(start);
    if (first == std::string::npos)
        return {};
    first += start.size();
    std::string::size_type last = source.find(end, first);
    return last == std::string::npos ? std::string() : source.substr(first, last - first);
}
}

int main()
{
    std::string const source = ReadSource();
    Check("source is readable", !source.empty());

    std::string const barrierWalk =
        Between(source, "OverseerDecisions::DungeonRunHoldsAtStage(state, DUNGEON_APPROACH_LIMITS);",
                "EscortToward(name, legAim.aim, \"BARRIER\", EscortPurpose::Assemble);");
    Check("the BARRIER walk is found", !barrierWalk.empty());
    Check("BARRIER ends the far hold before its escort claims the walk",
          barrierWalk.find("EndFarHold(name, \"its dungeon run walks it to the staging point\")") !=
              std::string::npos);

    std::string const innWalk =
        Between(source, "case HearthRegroupStep::Travel:",
                "EscortToward(name, innAim.str(), \"HEARTH REGROUP\", EscortPurpose::Assemble);");
    Check("the hearth regroup still ends the far hold before its walk",
          innWalk.find("EndFarHold(name, \"its dungeon run walks it to the family's inn\")") !=
              std::string::npos);

    return failures ? 1 : 0;
}
