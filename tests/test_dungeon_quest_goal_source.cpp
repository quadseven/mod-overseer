/*
 * Source contract for the dungeon quest goal's adapter paths (#972, #1015).
 *
 * The goal's own decision is proved in test_dungeon_complete.cpp. What only
 * the adapter can get wrong is how it feeds and acts on that decision, and the
 * adapter needs a worldserver to run, so the wiring is pinned as text:
 *
 *  - a goal quest with no ender on the outside map is skipped, not held: EXIT
 *    used to return false on it every poll, while also stamping
 *    last_progress_at, so the run never ended and no watchdog could end it;
 *  - an objective that cannot be resolved leaves the goal uncaptured, so the
 *    next poll asks again rather than running with half a goal;
 *  - an offline holder reads as not done, so the goal waits for them;
 *  - only map 36 has a quest goal, and every other map still finishes on its
 *    credited encounters.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int Fail(char const* what)
{
    std::cerr << what << "\n";
    return EXIT_FAILURE;
}

// The text of the member function that starts at `signature`, up to the next
// line that closes a member function at the class's indentation.
std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

// The text between `from` and the next `to` after it.
std::string Between(std::string const& text, char const* from, char const* to)
{
    std::size_t const start = text.find(from);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find(to, start + 1);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const turnIn = Body(text, "bool DriveDungeonQuestTurnIn(");
    if (turnIn.empty())
        return Fail("cannot find DriveDungeonQuestTurnIn");
    std::string const noEnder =
        Between(turnIn, "if (!FindDungeonQuestEnder(", "Player* holder");
    if (noEnder.empty())
        return Fail("DriveDungeonQuestTurnIn must look the ender up with FindDungeonQuestEnder");
    if (noEnder.find("return false") != std::string::npos)
        return Fail("a goal quest with no ender on the outside map must not hold EXIT for ever");
    if (noEnder.find("continue;") == std::string::npos ||
        noEnder.find("turnedInQuestIds.insert(questId)") == std::string::npos)
        return Fail("a goal quest with no ender must be settled once and skipped");

    std::string const capture = Body(text, "static void CaptureDungeonQuestGoal(");
    if (capture.empty())
        return Fail("cannot find CaptureDungeonQuestGoal");
    std::size_t const unresolved = capture.find("could not resolve objective");
    std::size_t const captured = capture.find("coord.questGoalCaptured = true;");
    if (unresolved == std::string::npos || captured == std::string::npos ||
        captured < unresolved)
        return Fail("an unresolved objective must leave the goal uncaptured for the next poll");
    if (capture.find("coord.questGoalCaptured", unresolved) != captured)
        return Fail("nothing may mark the goal captured before every objective resolved");

    std::string const complete = Body(text, "static bool DungeonQuestGoalComplete(");
    if (complete.empty())
        return Fail("cannot find DungeonQuestGoalComplete");
    if (complete.find("bool done = false;") == std::string::npos ||
        complete.find("if (player && quest") == std::string::npos ||
        complete.find("OverseerDecisions::DungeonQuestGoalReady(readings)") == std::string::npos)
        return Fail("an offline holder must read as not done and the decision must be asked");

    std::string const clearing =
        Between(text, "if (portal->insideMapId == 36)\n                {",
                "coord.provedComplete = true;");
    if (clearing.empty())
        return Fail("the CLEARING check must ask the quest goal on map 36 only");
    if (clearing.find("portal->insideMapId != 36 &&") == std::string::npos ||
        clearing.find("OverseerDecisions::DungeonCompletion::Complete") == std::string::npos)
        return Fail("every map but 36 must still finish on its credited encounters");

    return EXIT_SUCCESS;
}
