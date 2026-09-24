/*
 * A point aim left in a follower's column by a worldserver that has since
 * restarted (#658).
 *
 * Measured on the dev realm 2026-09-24. At 03:51 the far catch-up aimed three
 * Horde members at the spot their leader had hearthed to, as the book's own
 * claim. The worldserver restarted at about 05:29, the book forgot it had
 * written the aim, and:
 *
 *   05:30:06 the travel drive walked Oz, Uzza and Zork 2,282 yards back toward
 *            'at:1:-610.983,-4320.09,39.7389'
 *   "travel release for 'Uzza' skipped the column write - a profession errand
 *    (skill 182) and errand 'at:...' are outstanding and this book never
 *    claimed the aim it would have erased"
 *
 * and the aim stayed in all three columns for more than two hours.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, plus a read of
 * src/mod_overseer.cpp to pin the one sweep that asks.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::LeftoverAim;
using OverseerDecisions::LeftoverAimFacts;
using OverseerDecisions::LeftoverAimName;
using OverseerDecisions::ReadLeftoverAim;

namespace
{

int failures = 0;

void CheckVerdict(char const* what, LeftoverAimFacts const& facts, LeftoverAim want)
{
    LeftoverAim const got = ReadLeftoverAim(facts);
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, LeftoverAimName(got),
                LeftoverAimName(want));
    ++failures;
}

void Check(char const* what, bool got)
{
    if (got)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

LeftoverAimFacts Facts(char const* target, bool writtenHere, bool leads)
{
    LeftoverAimFacts f;
    f.target = target;
    f.writtenHere = writtenHere;
    f.leadsFamily = leads;
    return f;
}

char const* const MEASURED = "at:1:-610.983,-4320.09,39.7389";

void TheMeasuredCatchUpIsCleared()
{
    CheckVerdict("Oz's leftover catch-up", Facts(MEASURED, false, false), LeftoverAim::Release);
}

void WhatThisProcessWroteIsKept()
{
    CheckVerdict("written since start", Facts(MEASURED, true, false),
                 LeftoverAim::WrittenHere);
}

void TheLeadersPointIsKept()
{
    // A bridge pass's walk, or the run's staging aim #596 adopts.
    CheckVerdict("the leader's own", Facts(MEASURED, false, true), LeftoverAim::TheLeadersOwn);
}

void OnlyAPointIsEverCleared()
{
    char const* const others[] = {"vendor", "profession trainer", "innkeeper", "3310",
                                  "trigger:226", "flight master:64", ""};
    for (char const* t : others)
        CheckVerdict(t, Facts(t, false, false), LeftoverAim::NotAPoint);
    // The prefix, not a substring.
    CheckVerdict("a keyword containing at:", Facts("flat:1:2,3,4", false, false),
                 LeftoverAim::NotAPoint);
}

void EveryVerdictSaysWhich()
{
    LeftoverAim const all[] = {LeftoverAim::NotAPoint, LeftoverAim::WrittenHere,
                               LeftoverAim::TheLeadersOwn, LeftoverAim::Release};
    for (LeftoverAim v : all)
        Check("a verdict has a name", std::strcmp(LeftoverAimName(v), "unknown") != 0);
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

void TheFirstTravelPollSweeps()
{
    std::string const source = ReadModule();
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }
    std::size_t const drive = source.find("    void DriveTravel()\n");
    std::size_t const sweep = source.find("SweepLeftoverAims() ||", drive);
    std::size_t const load = source.find("_travelAims.Load();", drive);
    Check("DriveTravel exists", drive != std::string::npos);
    Check("DriveTravel sweeps", sweep != std::string::npos);
    Check("before its first read of the column", sweep < load);
    Check("once per process", source.find("if (!_leftoverAimsSwept)") != std::string::npos);
    // An empty read cannot be told from a failed one, so it is asked again for
    // a bounded number of polls rather than given up on at the first.
    Check("retried when nothing came back",
          source.find("SweepLeftoverAims() || ++_leftoverSweepPolls >= LEFTOVER_SWEEP_POLLS") !=
              std::string::npos);
    Check("an empty read does not count as done",
          source.find("return false;  // none, a failed read") != std::string::npos);
    Check("asks the decision",
          source.find("OverseerDecisions::ReadLeftoverAim(facts)") != std::string::npos);
    Check("the book's own claim is what 'written here' means",
          source.find("facts.writtenHere = _travelAims.RunOwns(name, target);") !=
              std::string::npos);
    // Cleared by a compare-and-swap on the aim, leaving learn_skill alone.
    Check("compare-and-swap",
          source.find("UPDATE overseer_roster SET travel_npc = '' WHERE name = '{}' AND "
                      "travel_npc = '{}' \"\n            \"AND enabled = 1\"") != std::string::npos);
}

}  // namespace

int main()
{
    TheMeasuredCatchUpIsCleared();
    WhatThisProcessWroteIsKept();
    TheLeadersPointIsKept();
    OnlyAPointIsEverCleared();
    EveryVerdictSaysWhich();
    TheFirstTravelPollSweeps();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a catch-up point from before a restart is not walked\n");
    return EXIT_SUCCESS;
}
