/*
 * The crossing drive asks the walk ratchet before it asks the count (#1414).
 *
 * test_crossing_walk.cpp proves the decision. This pins the wiring in
 * DriveDungeonCrossing: the farthest walker is read from the same census the
 * count is, CrossingWalkKeepsClock runs before the count's Ratchet, a true
 * answer moves the crossing's own clock, and the count's Ratchet is asked at
 * that same `now`. If the walk were asked after the count, the count would
 * already have stalled on the poll the walk would have saved.
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

    std::string const drive =
        Between(source, "DungeonCrossingResult DriveDungeonCrossing(",
                "if (OverseerDecisions::DungeonRunAllThrough(states))");
    Check("the crossing drive is found", !drive.empty());

    std::string::size_type const farthest =
        drive.find("OverseerDecisions::CrossingFarthestWalker(states)");
    std::string::size_type const walk =
        drive.find("OverseerDecisions::CrossingWalkKeepsClock(coord.crossingWalk, "
                   "coord.crossing.since");
    std::string::size_type const moved = drive.find("coord.crossing.since = now;");
    std::string::size_type const count = drive.find(
        "coord.crossing, static_cast<float>(through), now, DUNGEON_CROSSING_RATCHET");

    Check("the farthest walker is read from the census", farthest != std::string::npos);
    Check("the walk is asked with the crossing's own clock", walk != std::string::npos);
    Check("a walk that progressed moves that clock", moved != std::string::npos);
    Check("the count is asked at the same now", count != std::string::npos);
    Check("the walk is asked before the count",
          walk != std::string::npos && count != std::string::npos && walk < moved &&
              moved < count);

    Check("the coordinator keeps the walk beside the count",
          source.find("OverseerDecisions::CrossingWalkState crossingWalk;") !=
              std::string::npos);
    Check("the door aim line says what the floor probe found",
          source.find("\"door's own box (#376). The floor probe {} (#1414)\"") !=
              std::string::npos);

    return failures ? 1 : 0;
}
