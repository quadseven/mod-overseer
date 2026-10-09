/*
 * A natural guild member's ghost whose corpse lies inside a dungeon.
 *
 * The live failure this pins: the guild ghost drive read the corpse through
 * Player::GetCorpse(), which resolves through the map the ghost stands on, and
 * skipped a null as "not released yet". A guild run that wiped disbanded its
 * group, the dead released to the graveyard outside, and their corpses stayed
 * on the instance map. Measured on the dev realm (2026-10-09):
 *
 *   21 online guild members were ghosts at the graveyards outside the
 *   Deadmines (map 36), Wailing Caverns (map 43) and Blackfathom Deeps
 *   (map 48), none in a group, the oldest dead for nearly six hours.
 *   One guild's tank stood at the Westfall graveyard on map 0 from 09:29
 *   with its corpse on map 36; another guild's healer stood at the Barrens
 *   graveyard on map 1 with its corpse on map 43.
 *   253 of 256 guild deaths on a dungeon map in two days carried no ghost
 *   recovery decision at all.
 *
 * Out of the group the instance is no longer theirs, so the corpse cannot be
 * reached: the spirit healer is the way back. A grouped ghost is left to the
 * run that may still take it in, and a corpse on the ghost's own map is
 * DecideGhostRecovery's to judge.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, and reads
 * src/mod_overseer.cpp to pin the wiring (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::StrandedGhostFacts;
using OverseerDecisions::StrandedGhostNext;
using OverseerDecisions::StrandedGhostStep;
using OverseerDecisions::StrandedGhostStepWord;

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

void Expect(char const* what, StrandedGhostFacts const& facts, StrandedGhostStep want)
{
    StrandedGhostStep const got = StrandedGhostNext(facts);
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, StrandedGhostStepWord(got),
                StrandedGhostStepWord(want));
    ++failures;
}

// The tank at the Westfall graveyard: released, its corpse in the Deadmines,
// standing on map 0, the run's group disbanded.
StrandedGhostFacts TheTankOutsideTheDeadmines()
{
    StrandedGhostFacts f;
    f.ghost = true;
    f.corpseKnown = true;
    f.corpseMapId = 36;
    f.standingMapId = 0;
    f.standingInstanceable = false;
    f.grouped = false;
    return f;
}

void TheMeasuredGhostsTakeTheSpiritHealer()
{
    Expect("a ghost outside the Deadmines with its corpse inside takes the spirit healer",
           TheTankOutsideTheDeadmines(), StrandedGhostStep::SpiritHealer);

    StrandedGhostFacts f = TheTankOutsideTheDeadmines();
    f.corpseMapId = 43;
    f.standingMapId = 1;
    Expect("so does one at the Barrens graveyard with its corpse in Wailing Caverns", f,
           StrandedGhostStep::SpiritHealer);

    f.corpseMapId = 48;
    Expect("and one with its corpse in Blackfathom Deeps", f, StrandedGhostStep::SpiritHealer);
}

void EverythingElseIsLeftAlone()
{
    StrandedGhostFacts f = TheTankOutsideTheDeadmines();
    f.grouped = true;
    Expect("a ghost still in its group is left to the run that may take it back", f,
           StrandedGhostStep::Leave);

    f = TheTankOutsideTheDeadmines();
    f.ghost = false;
    Expect("a body not yet released is the dead engine's to release", f,
           StrandedGhostStep::Leave);

    f = TheTankOutsideTheDeadmines();
    f.corpseKnown = false;
    Expect("a ghost with no corpse the core knows of is left alone", f,
           StrandedGhostStep::Leave);

    f = TheTankOutsideTheDeadmines();
    f.corpseMapId = 0;
    Expect("a corpse on the map the ghost stands on is DecideGhostRecovery's", f,
           StrandedGhostStep::Leave);

    f = TheTankOutsideTheDeadmines();
    f.standingMapId = 36;
    f.corpseMapId = 0;
    f.standingInstanceable = true;
    Expect("a ghost standing inside an instance is left to that instance's recovery", f,
           StrandedGhostStep::Leave);

    Expect("an empty reading leaves it alone", StrandedGhostFacts{}, StrandedGhostStep::Leave);
}

void TheWordsAreFixed()
{
    Check("the leave word", std::strcmp(StrandedGhostStepWord(StrandedGhostStep::Leave),
                                        "leave") == 0);
    Check("the spirit healer word is the ghost_recovery column's",
          std::strcmp(StrandedGhostStepWord(StrandedGhostStep::SpiritHealer),
                      "spirit_healer") == 0);
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

void TheAdapterIsWired()
{
    std::string const source = ReadModule();
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }
    std::size_t const begin = source.find("    void DriveGuildGhostRecovery()");
    std::size_t const end = source.find("    void DriveStrandedGhost(", begin);
    Check("DriveGuildGhostRecovery is followed by DriveStrandedGhost",
          begin != std::string::npos && end != std::string::npos);
    if (begin == std::string::npos || end == std::string::npos)
        return;
    std::string const drive = source.substr(begin, end - begin);
    Check("a null map-scoped corpse is handed to the stranded ghost drive",
          drive.find("if (!corpse)\n            {\n                DriveStrandedGhost(bot, botAI, "
                     "name, now);\n                continue;\n            }") !=
              std::string::npos);
    Check("and is no longer skipped as not released",
          drive.find("continue;  // not released yet") == std::string::npos);

    std::size_t const strandedEnd = source.find("    bool LeaveGuildDeathSpot(", end);
    Check("DriveStrandedGhost ends before LeaveGuildDeathSpot", strandedEnd != std::string::npos);
    if (strandedEnd == std::string::npos)
        return;
    std::string const stranded = source.substr(end, strandedEnd - end);
    auto has = [&](char const* what, char const* text) {
        Check(what, stranded.find(text) != std::string::npos);
    };
    has("the corpse is read where the core keeps it across maps",
        "bot->GetCorpseLocation();");
    has("the decision is the pure one", "OverseerDecisions::StrandedGhostNext(facts)");
    has("the group is a fact", "facts.grouped = bot->GetGroup() != nullptr;");
    has("the spirit healer is walked to as for any ghost",
        "WalkGhostToSpiritHealer(bot, botAI, name, st, /*ladder*/ false, now);");
    has("the deploy proof is said", "overseer: stranded ghost '{}'");
    Check("and nothing is granted: no teleport", stranded.find("TeleportTo") == std::string::npos);
    Check("and no resurrection by hand", stranded.find("ResurrectPlayer") == std::string::npos);

    // The healer walk is the core's own handler, shared with DriveGhostRecovery.
    Check("DriveGhostRecovery hands the spirit healer to the shared walk",
          source.find("        return WalkGhostToSpiritHealer(bot, botAI, name, st, ladder, now);") !=
              std::string::npos);
    std::size_t const walk = source.find("    bool WalkGhostToSpiritHealer(");
    Check("the shared walk exists", walk != std::string::npos);
    if (walk != std::string::npos)
        Check("and asks the spirit healer as a client asks it",
              source.find("HandleSpiritHealerActivateOpcode(packet);", walk) !=
                  std::string::npos);
}

}  // namespace

int main()
{
    TheMeasuredGhostsTakeTheSpiritHealer();
    EverythingElseIsLeftAlone();
    TheWordsAreFixed();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("stranded ghost: all checks passed\n");
    return 0;
}
