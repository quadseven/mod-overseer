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
 * reached: the spirit healer is the way back. A grouped ghost is left to a
 * run that may still take it in (a guild run listing it, or the raid
 * run-back inside its window), not to the group alone (wow-overseer#1093),
 * and a corpse on the ghost's own map is DecideGhostRecovery's to judge.
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

using OverseerDecisions::GuildFinderRequest;
using OverseerDecisions::GuildFinderSeatsName;
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

// wow-overseer#1093, measured on the dev realm (2026-10-18): four guild
// members stood as ghosts at the graveyard outside Blackfathom Deeps for three
// to five days, their corpses on map 48, at least one still leading a group
// that no run owned. A group is not a run, and nothing else walks them.
void AGroupNoRunOwnsDoesNotHoldTheGhost()
{
    StrandedGhostFacts f = TheTankOutsideTheDeadmines();
    f.corpseMapId = 48;
    f.standingMapId = 1;
    f.grouped = true;
    f.runTakesBack = false;
    Expect("a grouped ghost no run is taking back takes the spirit healer", f,
           StrandedGhostStep::SpiritHealer);

    f.runTakesBack = true;
    Expect("a grouped ghost a run is taking back is left to the run", f,
           StrandedGhostStep::Leave);

    f.grouped = false;
    Expect("out of the group the instance is not the run's to take it back into", f,
           StrandedGhostStep::SpiritHealer);
}

// mod-overseer#1046, found by the roll-63 pre-roll review: after a restart a
// guild run waits up to 15 minutes to be adopted, known only by its tank and
// its row's request, and in that wait runTakesBack read only the live runs. A
// member that had released outside walked to the spirit healer, and a run that
// had wiped was then released with nobody of it back inside. Every seat of the
// waiting run counts, and nobody else does.
void AWaitingAdoptionTakesItsGhostsBack()
{
    GuildFinderRequest request;
    request.healer = "Healer";
    request.damage = {"Damage", "Hunter", "Mage"};
    request.pugs = {"Mage"};
    Check("the waiting run's tank is seated", GuildFinderSeatsName("Tank", request, "Tank"));
    Check("so is its healer", GuildFinderSeatsName("Tank", request, "Healer"));
    Check("and each damage dealer", GuildFinderSeatsName("Tank", request, "Damage") &&
                                        GuildFinderSeatsName("Tank", request, "Hunter"));
    Check("a pug among them too", GuildFinderSeatsName("Tank", request, "Mage"));
    Check("names compare without case", GuildFinderSeatsName("Tank", request, "hEALER") &&
                                             GuildFinderSeatsName("tank", request, "TANK"));
    Check("a name the run does not seat is not taken back",
          !GuildFinderSeatsName("Tank", request, "Stranger"));
    Check("an empty request seats only its tank",
          !GuildFinderSeatsName("Tank", GuildFinderRequest{}, "Healer"));

    // The decision it feeds: a grouped ghost the waiting run seats is left to
    // that run, and one no run, adoption or run-back takes (the #1023 case)
    // still takes the spirit healer.
    StrandedGhostFacts f = TheTankOutsideTheDeadmines();
    f.grouped = true;
    f.runTakesBack = GuildFinderSeatsName("Tank", request, "Hunter");
    Expect("a ghost a waiting adoption seats is left to the run", f, StrandedGhostStep::Leave);
    f.runTakesBack = GuildFinderSeatsName("Tank", request, "Stranger");
    Expect("a grouped ghost no run or adoption seats still takes the spirit healer", f,
           StrandedGhostStep::SpiritHealer);
}

void EverythingElseIsLeftAlone()
{
    StrandedGhostFacts f = TheTankOutsideTheDeadmines();
    f.grouped = true;
    f.runTakesBack = true;
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
    has("so is a run that may take it back, a waiting adoption among them (#1046)",
        "facts.runTakesBack =\n"
        "            InAGuildRun(name) || InAWaitingGuildRun(name) || RaidRunBackWalks(name, now);");

    std::size_t const waiting = source.find("    bool InAWaitingGuildRun(std::string const& name) const");
    Check("the waiting adoptions are read by name", waiting != std::string::npos);
    if (waiting != std::string::npos)
    {
        std::string const reader = source.substr(waiting, 400);
        Check("over every run waiting to be adopted",
              reader.find("for (GuildRunAdoption const& adoption : _guildRunAdoptions)") !=
                  std::string::npos);
        Check("by every seat of its request",
              reader.find("OverseerDecisions::GuildFinderSeatsName(adoption.tank, "
                          "adoption.request, name)") != std::string::npos);
    }
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
    AGroupNoRunOwnsDoesNotHoldTheGhost();
    AWaitingAdoptionTakesItsGhostsBack();
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
