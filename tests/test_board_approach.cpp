/*
 * The step aboard is taken from the berth itself, by BOTH crossings (#1018).
 *
 * WHAT WAS MEASURED. A crossing reads "at the berth" from twelve yards out. On
 * the dev realm a step aboard the Menethil boat from 11.6 and 12.5 yards left
 * the walker 28 yards out and not aboard, both times, and one from 7.8 yards
 * sailed (#913). #913 closed the walker on the berth first, but only in the
 * member crossing (DriveLoneCrossing). The run leader's crossing
 * (DriveContinentCrossing) stepped him aboard from wherever he stood inside
 * the twelve yards: on 2026-10-18 he stepped aboard the Lady Mehley from 13.3
 * yards, was not aboard 25 seconds later, read further out than the berth
 * tolerance, and was walked back to the pier with the ship still docked. Every
 * Scarlet Monastery run from Kalimdor then closed `staging_failed` (#758,
 * #431).
 *
 * The first half proves the decision. The second half pins, as text, that both
 * crossings ask it in the steps where the leader waits for and steps onto the
 * boat: a decision nothing feeds is the failure this repository has shipped
 * before, and the leader's crossing is exactly the call site #913 missed.
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

// The text of one `case` in `body`, up to the next `case` label.
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

    // The measured misses and the measured success, all level with the deck.
    CheckApproach("12.5 yards out, the measured miss: close first",
                  BoardApproachFrom(true, 12.5f, 0, limits), BoardApproach::CloseOnBerth);
    CheckApproach("11.6 yards out, the measured miss: close first",
                  BoardApproachFrom(true, 11.6f, 0, limits), BoardApproach::CloseOnBerth);
    CheckApproach("7.8 yards out is still more than a short step: close first",
                  BoardApproachFrom(true, 7.8f, 0, limits), BoardApproach::CloseOnBerth);
    CheckApproach("3.9 yards out: step aboard",
                  BoardApproachFrom(true, 3.9f, 0, limits), BoardApproach::StepAboard);
    CheckApproach("on the berth: step aboard",
                  BoardApproachFrom(true, 0.0f, 0, limits), BoardApproach::StepAboard);
    CheckApproach("exactly the limit is close enough",
                  BoardApproachFrom(true, 4.0f, 0, limits), BoardApproach::StepAboard);

    // Bounded: a berth nobody can stand near never stops the boat being boarded.
    CheckApproach("12.5 yards out after five approaches: still closing",
                  BoardApproachFrom(true, 12.5f, 5, limits), BoardApproach::CloseOnBerth);
    CheckApproach("12.5 yards out after six approaches: board from where he stands",
                  BoardApproachFrom(true, 12.5f, 6, limits), BoardApproach::StepAboard);

    // Level first and unbounded: a walker a level off the deck is never one
    // step from it, however many polls have passed.
    CheckApproach("on the berth but a level off it: close first",
                  BoardApproachFrom(false, 1.0f, 0, limits), BoardApproach::CloseOnBerth);
    CheckApproach("a level off it after every approach: still close first",
                  BoardApproachFrom(false, 1.0f, 99, limits), BoardApproach::CloseOnBerth);

    // A limit nobody set fails closed: no approaches, so nobody is held off
    // the boat, and nobody a level off it is stepped aboard.
    BoardApproachLimits const unset;
    CheckApproach("unset limits: a level walker steps aboard",
                  BoardApproachFrom(true, 12.5f, 0, unset), BoardApproach::StepAboard);
    CheckApproach("unset limits: a walker a level off still closes",
                  BoardApproachFrom(false, 0.0f, 0, unset), BoardApproach::CloseOnBerth);
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

    Check("CROSSING_BOARD_APPROACH is built from the #913 numbers",
          text.find("CROSSING_BOARD_APPROACH{\n    CROSSING_BOARD_FROM_BERTH_YARDS, "
                    "CROSSING_BOARD_APPROACH_TRIES}") != std::string::npos);

    for (char const* drive : {"void DriveContinentCrossing(", "void DriveLoneCrossing("})
    {
        std::string const body = Body(text, drive);
        if (body.empty())
        {
            std::fprintf(stderr, "FAIL: cannot find %s\n", drive);
            ++failures;
            continue;
        }
        for (char const* label : {"case OverseerDecisions::CrossingAction::Hold:",
                                  "case OverseerDecisions::CrossingAction::Board:"})
        {
            std::string const step = Case(body, label);
            bool const asks = step.find("OverseerDecisions::BoardApproachFrom(") !=
                                  std::string::npos &&
                              step.find("CROSSING_BOARD_APPROACH") != std::string::npos;
            if (!asks)
            {
                std::fprintf(stderr, "FAIL: %s %s must close on the berth with "
                                     "BoardApproachFrom(..., CROSSING_BOARD_APPROACH...)\n",
                             drive, label);
                ++failures;
            }
        }
    }

    // The leader's Board counts its approaches, so the close is bounded.
    std::string const board = Case(Body(text, "void DriveContinentCrossing("),
                                   "case OverseerDecisions::CrossingAction::Board:");
    Check("the leader's Board passes and counts coord.crossingBoardApproaches",
          board.find("coord.crossingBoardApproaches,") != std::string::npos &&
              board.find("++coord.crossingBoardApproaches;") != std::string::npos);
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
    std::printf("test_board_approach: ok\n");
    return EXIT_SUCCESS;
}
