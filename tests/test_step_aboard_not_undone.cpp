/*
 * A step aboard taken this stop is not undone by the next poll (#758).
 *
 * WHAT WAS MEASURED. On the dev realm on 2026-10-24 a member at the Azuremyst
 * dock (map 530) stepped aboard a docked ship from the berth onto a deck edge
 * 9.2 yards out, four times in twelve minutes. Each time, fifteen seconds
 * later and with the ship still docked, he read 28 yards from the berth, the
 * crossing said Walk, and his cross hold was released ("it is walking to the
 * berth again"). After three such walks the row ended "the walk to the berth
 * gave up on the ground". Members stepping aboard onto deck edges 3.9 yards
 * from their berths sailed, though one of them missed twice first.
 *
 * WHY THE DISTANCE MATTERS. The step aboard is taken only within
 * CROSSING_BOARD_FROM_BERTH_YARDS (4) of the berth (#913, #1018), and the
 * same question was asked again on every Board poll. A walker the step had
 * moved onto a deck edge 9.2 yards out, and that the bot AI had not attached
 * yet, read "close on the berth first", and the pathed step back toward the
 * berth took him off the deck. A 3.9-yard deck edge is inside the 4 yards, so
 * the same miss there was simply stepped again.
 *
 * The first half proves the decision. The second half pins, as text, that both
 * crossings' Board steps pass the "stepped aboard this stop" fact and that each
 * sets it on the step and clears it when the reading leaves Board.
 *
 * Compiled against src/overseer_decisions.cpp and run from the repository root.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::BoardApproach;
using OverseerDecisions::BoardApproachFrom;
using OverseerDecisions::BoardApproachLimits;
using OverseerDecisions::BoardApproachName;

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void CheckApproach(char const* what, BoardApproach got, BoardApproach want)
{
    if (got != want)
    {
        std::fprintf(stderr, "FAIL: %s: got '%s', want '%s'\n", what, BoardApproachName(got),
                     BoardApproachName(want));
        ++failures;
    }
}

// The module's own numbers (CROSSING_BOARD_FROM_BERTH_YARDS,
// CROSSING_BOARD_APPROACH_TRIES in src/mod_overseer.cpp).
BoardApproachLimits Limits()
{
    BoardApproachLimits limits;
    limits.fromBerthYards = 4.0f;
    limits.maxApproaches = 6;
    return limits;
}

std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

std::string Case(std::string const& body, char const* label)
{
    std::size_t const start = body.find(label);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = body.find("case OverseerDecisions::CrossingAction::",
                                      start + std::string(label).size());
    return body.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

void Decision()
{
    BoardApproachLimits const limits = Limits();

    // The measured case: on the deck edge 9.2 yards from the berth, level with
    // it, not attached yet, the ship still docked. He is stepped again.
    CheckApproach("stepped aboard this stop, on a deck edge 9.2 yards out: step again",
                  BoardApproachFrom(true, 9.2f, 1, limits, true), BoardApproach::StepAboard);
    CheckApproach("stepped aboard this stop, no approaches spent: step again",
                  BoardApproachFrom(true, 9.2f, 0, limits, true), BoardApproach::StepAboard);
    CheckApproach("stepped aboard this stop, anywhere in the hull's reach: step again",
                  BoardApproachFrom(true, 30.0f, 0, limits, true), BoardApproach::StepAboard);

    // In the water, a level off the deck: he closes on the berth whatever the
    // step did, because a walker below the deck is never one step from it.
    CheckApproach("stepped aboard this stop but a level off the berth: close first",
                  BoardApproachFrom(false, 9.2f, 0, limits, true), BoardApproach::CloseOnBerth);

    // Before any step this stop, #913's rule is unchanged.
    CheckApproach("not stepped yet, 9.2 yards out: close first",
                  BoardApproachFrom(true, 9.2f, 0, limits, false), BoardApproach::CloseOnBerth);
    CheckApproach("not stepped yet, the default: close first",
                  BoardApproachFrom(true, 9.2f, 0, limits), BoardApproach::CloseOnBerth);
    CheckApproach("not stepped yet, 3.9 yards out: step aboard",
                  BoardApproachFrom(true, 3.9f, 0, limits, false), BoardApproach::StepAboard);
}

void Wiring()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
    {
        Check("cannot read src/mod_overseer.cpp", false);
        return;
    }
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    // THE MEMBER CROSSING (DriveLoneCrossing), where the 2026-10-24 misses were.
    std::string const lone = Body(text, "void DriveLoneCrossing(");
    Check("DriveLoneCrossing is found", !lone.empty());
    std::string const loneBoard = Case(lone, "case OverseerDecisions::CrossingAction::Board:");
    Check("the member's Board passes memo.boardStepped to BoardApproachFrom",
          loneBoard.find("CROSSING_BOARD_APPROACH, memo.boardStepped)") != std::string::npos);
    std::size_t const move = loneBoard.find("MovePoint(CROSSING_STEP_POINT_ID");
    std::size_t const set = loneBoard.find("memo.boardStepped = true;");
    Check("the member's Board sets memo.boardStepped after the step aboard",
          move != std::string::npos && set != std::string::npos && set > move);
    Check("the member crossing clears memo.boardStepped when the reading is not Board",
          lone.find("if (step.action != OverseerDecisions::CrossingAction::Board)\n"
                    "            memo.boardStepped = false;") != std::string::npos);
    Check("a lone crossing's memo starts with boardStepped false",
          text.find("bool boardStepped{false};") != std::string::npos);

    // THE RUN LEADER'S CROSSING (DriveContinentCrossing), the same step.
    std::string const leader = Body(text, "void DriveContinentCrossing(");
    Check("DriveContinentCrossing is found", !leader.empty());
    std::string const leaderBoard =
        Case(leader, "case OverseerDecisions::CrossingAction::Board:");
    Check("the leader's Board passes coord.crossingBoardStepSaid to BoardApproachFrom",
          leaderBoard.find("CROSSING_BOARD_APPROACH,\n") != std::string::npos &&
              leaderBoard.find("coord.crossingBoardStepSaid) ==") != std::string::npos);
    std::size_t const leaderMove = leaderBoard.find("MovePoint(CROSSING_STEP_POINT_ID");
    std::size_t const leaderSet = leaderBoard.find("coord.crossingBoardStepSaid = true;");
    Check("the leader's Board sets crossingBoardStepSaid after the step aboard",
          leaderMove != std::string::npos && leaderSet != std::string::npos &&
              leaderSet > leaderMove);
    Check("the leader's crossing clears crossingBoardStepSaid when the reading is not Board",
          leader.find("if (step.action != OverseerDecisions::CrossingAction::Board)\n"
                      "        {\n"
                      "            coord.crossingBoardApproaches = 0;\n"
                      "            coord.crossingBoardStepSaid = false;") != std::string::npos);

    // WAITING FOR THE BOAT IS NOT A STEP: both Holds ask without the fact, so
    // a walker left on a deck edge when the stop runs out closes on the berth.
    for (std::string const* body : {&lone, &leader})
    {
        std::string const hold = Case(*body, "case OverseerDecisions::CrossingAction::Hold:");
        Check("a Hold does not pass a stepped-aboard fact",
              hold.find("boardStepped") == std::string::npos &&
                  hold.find("crossingBoardStepSaid") == std::string::npos);
    }
}
}  // namespace

int main()
{
    Decision();
    Wiring();
    if (failures)
    {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("test_step_aboard_not_undone: ok\n");
    return EXIT_SUCCESS;
}
