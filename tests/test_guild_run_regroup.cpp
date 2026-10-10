/*
 * A guild run goes back in together after a wipe, and rests before it pulls.
 *
 * The live failure this pins. In the Deadmines guild runs between the corpse
 * run's deploy and 2026-10-10 00:00 (America/New_York), 17 runs cleared 0 and
 * took 157 deaths. 13 of them wiped to the last member, and 58 deaths came
 * after that wipe, 52 of them the tank's:
 *
 *   run 462: Sneed was pulled with the lowest caster at 3% mana and all five
 *     died. The tank's ghost reached the door 81 s after the release, stepped
 *     through alone, was raised at the entrance, and the dungeon brain it
 *     still carried walked it back into the lumber mill. It died three more
 *     times. The other four ghosts never reached the door in the 900 s window
 *     and took the spirit healer when the run ended.
 *   run 464: four of five died around Sneed. The tank walked in alone to
 *     the healer and both died 3 minutes later; three ghosts had trailed the tank's over the
 *     hill above the mine (the dead engine's `follow`) and stood 17 to 20
 *     yards over the door's trigger.
 *
 * A group of players runs back together: the ghosts wait at the door until
 * the healer and enough of the group are there, walk in together, and rest to
 * full before the next pull. A party with a caster at 3% mana drinks first.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, and reads
 * src/mod_overseer.cpp to pin the wiring (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::GUILD_RUN_REGROUP_WAIT_SECONDS;
using OverseerDecisions::GUILD_RUN_REST_MAX_SECONDS;
using OverseerDecisions::GuildRunDoorFacts;
using OverseerDecisions::GuildRunEnterTogether;
using OverseerDecisions::GuildRunHold;
using OverseerDecisions::GuildRunHoldFacts;
using OverseerDecisions::GuildRunHoldNext;
using OverseerDecisions::GuildRunHoldStep;
using OverseerDecisions::GuildRunHoldWhy;
using OverseerDecisions::GuildRunHoldWhyWord;
using OverseerDecisions::GuildRunMemberShape;
using OverseerDecisions::GuildRunRestNeed;
using OverseerDecisions::GuildRunRestNeedOf;

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

void ExpectHold(char const* what, GuildRunHoldFacts const& facts, GuildRunHoldStep step,
                GuildRunHoldWhy why)
{
    GuildRunHold const got = GuildRunHoldNext(facts);
    if (got.step == step && got.why == why)
        return;
    std::printf("FAIL %s: got step %d (%s), wanted step %d (%s)\n", what,
                static_cast<int>(got.step), GuildRunHoldWhyWord(got.why),
                static_cast<int>(step), GuildRunHoldWhyWord(why));
    ++failures;
}

GuildRunMemberShape Member(float health, float mana, bool usesMana, bool healer)
{
    GuildRunMemberShape m;
    m.healthPct = health;
    m.manaPct = mana;
    m.usesMana = usesMana;
    m.healer = healer;
    return m;
}

// ----------------------------------------------------------- the door --

// Run 462 after its first wipe: five ghosts walking back, nobody alive inside.
GuildRunDoorFacts Run462AtTheDoor(unsigned atDoor, bool healerAtDoor)
{
    GuildRunDoorFacts door;
    door.ghostsComingBack = 5;
    door.ghostsAtDoor = atDoor;
    door.aliveInside = 0;
    door.healerHere = healerAtDoor;
    door.groupSize = 5;
    return door;
}

void NobodyStepsThroughAlone()
{
    Check("run 462's tank at the door alone waits for the group",
          !GuildRunEnterTogether(Run462AtTheDoor(1, false)));
    Check("the tank and one more without the healer still wait",
          !GuildRunEnterTogether(Run462AtTheDoor(2, false)));
    Check("the healer and one more are not yet a group",
          !GuildRunEnterTogether(Run462AtTheDoor(2, true)));
    Check("nobody at the door steps through", !GuildRunEnterTogether(Run462AtTheDoor(0, true)));
}

void TheHealerOrAQuorumTakesThemIn()
{
    Check("the healer and two more go in together",
          GuildRunEnterTogether(Run462AtTheDoor(3, true)));
    Check("three without the healer still wait",
          !GuildRunEnterTogether(Run462AtTheDoor(3, false)));
    Check("four without the healer are a quorum", GuildRunEnterTogether(Run462AtTheDoor(4, false)));
    Check("all five go in", GuildRunEnterTogether(Run462AtTheDoor(5, true)));
}

void ALateGhostJoinsALivingGroup()
{
    GuildRunDoorFacts late;
    late.ghostsComingBack = 1;
    late.ghostsAtDoor = 1;
    late.aliveInside = 4;
    late.healerHere = true;
    late.groupSize = 5;
    Check("a ghost at the door joins four living inside", GuildRunEnterTogether(late));

    GuildRunDoorFacts two;
    two.ghostsComingBack = 2;
    two.ghostsAtDoor = 2;
    two.groupSize = 5;
    Check("two ghosts whose group has gone are never a group",
          !GuildRunEnterTogether(two));
}

// ---------------------------------------------------- resting first --

void ACasterAtThreePercentDrinksFirst()
{
    // Run 462 at Sneed (03:35:28): tank 95% health, healer 75% mana, lowest
    // caster 3% mana.
    std::vector<GuildRunMemberShape> const sneed = {
        Member(95, 0, false, false), Member(100, 75, true, true), Member(100, 3, true, false),
        Member(100, 60, true, false), Member(100, 70, true, false)};
    Check("a caster at 3% mana needs a rest", GuildRunRestNeedOf(sneed) == GuildRunRestNeed::Low);

    std::vector<GuildRunMemberShape> const healer = {
        Member(100, 0, false, false), Member(100, 55, true, true), Member(100, 90, true, false)};
    Check("a healer at 55% mana needs a rest", GuildRunRestNeedOf(healer) == GuildRunRestNeed::Low);

    std::vector<GuildRunMemberShape> const hurt = {
        Member(60, 0, false, false), Member(100, 100, true, true)};
    Check("a tank at 60% health needs a rest", GuildRunRestNeedOf(hurt) == GuildRunRestNeed::Low);

    std::vector<GuildRunMemberShape> const warm = {
        Member(85, 0, false, false), Member(100, 70, true, true), Member(100, 50, true, false)};
    Check("over every trigger and under the bars is part rested",
          GuildRunRestNeedOf(warm) == GuildRunRestNeed::Partial);

    std::vector<GuildRunMemberShape> const full = {
        Member(95, 0, false, false), Member(100, 90, true, true), Member(92, 85, true, false)};
    Check("over every bar is rested", GuildRunRestNeedOf(full) == GuildRunRestNeed::Rested);

    std::vector<GuildRunMemberShape> const rage = {Member(100, 0, false, false)};
    Check("a warrior's empty mana bar is not a rest",
          GuildRunRestNeedOf(rage) == GuildRunRestNeed::Rested);
}

GuildRunHoldFacts FiveAliveAt(GuildRunRestNeed need)
{
    GuildRunHoldFacts facts;
    facts.armed = true;
    facts.aliveInside = 5;
    facts.need = need;
    return facts;
}

void TheBrainHoldsWhileTheGroupRests()
{
    ExpectHold("a low group out of combat stops for a rest", FiveAliveAt(GuildRunRestNeed::Low),
               GuildRunHoldStep::Hold, GuildRunHoldWhy::Rest);
    ExpectHold("a part rested group pulls on", FiveAliveAt(GuildRunRestNeed::Partial),
               GuildRunHoldStep::Nothing, GuildRunHoldWhy::None);

    GuildRunHoldFacts fighting = FiveAliveAt(GuildRunRestNeed::Low);
    fighting.anyFighting = true;
    ExpectHold("a fight is the dungeon brain's", fighting, GuildRunHoldStep::Nothing,
               GuildRunHoldWhy::None);

    GuildRunHoldFacts resting = FiveAliveAt(GuildRunRestNeed::Partial);
    resting.held = true;
    resting.secondsResting = 30;
    ExpectHold("a rest goes on to the bars, past the triggers", resting, GuildRunHoldStep::Nothing,
               GuildRunHoldWhy::Rest);
    resting.need = GuildRunRestNeed::Rested;
    ExpectHold("rested, the group pulls again", resting, GuildRunHoldStep::Release,
               GuildRunHoldWhy::Rested);

    GuildRunHoldFacts unarmed = FiveAliveAt(GuildRunRestNeed::Low);
    unarmed.armed = false;
    ExpectHold("nothing is held before the brain is armed", unarmed, GuildRunHoldStep::Nothing,
               GuildRunHoldWhy::None);
}

void ARestCannotStallTheRun()
{
    GuildRunHoldFacts stuck = FiveAliveAt(GuildRunRestNeed::Low);
    stuck.held = true;
    stuck.secondsResting = GUILD_RUN_REST_MAX_SECONDS - 1;
    ExpectHold("a slow rest is waited out", stuck, GuildRunHoldStep::Nothing,
               GuildRunHoldWhy::Rest);
    stuck.secondsResting = GUILD_RUN_REST_MAX_SECONDS;
    ExpectHold("a rest that cannot finish lets the group go", stuck, GuildRunHoldStep::Release,
               GuildRunHoldWhy::RestTimedOut);

    GuildRunHoldFacts after = FiveAliveAt(GuildRunRestNeed::Low);
    after.restRearmed = false;
    ExpectHold("and the same member does not stop it again at once", after,
               GuildRunHoldStep::Nothing, GuildRunHoldWhy::None);
}

void TheGroupWaitsForItsGhostsBeforeItPulls()
{
    GuildRunHoldFacts regroup = FiveAliveAt(GuildRunRestNeed::Rested);
    regroup.aliveInside = 3;
    regroup.ghostsComingBack = true;
    regroup.secondsRegrouping = 10;
    ExpectHold("three raised wait for the two running back", regroup, GuildRunHoldStep::Hold,
               GuildRunHoldWhy::Regroup);
    regroup.held = true;
    ExpectHold("and keep waiting", regroup, GuildRunHoldStep::Nothing, GuildRunHoldWhy::Regroup);
    regroup.secondsRegrouping = GUILD_RUN_REGROUP_WAIT_SECONDS;
    ExpectHold("a ghost that does not come is not waited for forever", regroup,
               GuildRunHoldStep::Release, GuildRunHoldWhy::Rested);

    GuildRunHoldFacts low = regroup;
    low.need = GuildRunRestNeed::Low;
    ExpectHold("past the wait, a group raised at half health still rests", low,
               GuildRunHoldStep::Nothing, GuildRunHoldWhy::Rest);

    GuildRunHoldFacts wiped = FiveAliveAt(GuildRunRestNeed::Low);
    wiped.held = true;
    wiped.aliveInside = 0;
    wiped.ghostsComingBack = true;
    ExpectHold("a hold outlives a wipe, so the first raised cannot walk on alone", wiped,
               GuildRunHoldStep::Nothing, GuildRunHoldWhy::None);

    GuildRunHoldFacts body = FiveAliveAt(GuildRunRestNeed::Low);
    body.held = true;
    body.aliveInside = 4;
    body.deadInside = 1;
    ExpectHold("a body inside is the dungeon brain's own rez to walk to", body,
               GuildRunHoldStep::Release, GuildRunHoldWhy::Dead);
    body.held = false;
    ExpectHold("and is not held for", body, GuildRunHoldStep::Nothing, GuildRunHoldWhy::None);
}

// ---------------------------------------------------------- the wiring --

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

std::string Between(std::string const& source, char const* from, char const* to)
{
    std::size_t const begin = source.find(from);
    std::size_t const end = begin == std::string::npos ? begin : source.find(to, begin);
    if (begin == std::string::npos || end == std::string::npos)
        return std::string();
    return source.substr(begin, end - begin);
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
    std::string const recovery = Between(source, "    static bool GuildRunGhostOf(",
                                         "    bool DriveGuildRun(GuildRun& run, std::time_t now)");
    Check("the corpse run's functions are found", !recovery.empty());
    auto has = [&](char const* what, std::string const& text, char const* needle) {
        Check(what, text.find(needle) != std::string::npos);
    };
    has("the door asks the pure rule", recovery, "OverseerDecisions::GuildRunEnterTogether(door)");
    has("a ghost is taken off the dead engine's follow, which trails the tank", recovery,
        "GUILD_RUN_GHOST_LEASES[] = {\"dead\", \"follow\", \"stay\"}");
    has("the hold asks the pure rule", recovery, "OverseerDecisions::GuildRunHoldNext(facts)");
    has("the hold is the dungeon module's own pause", recovery,
        "DoSpecificAction(\"dc pause\", Event(\"dc\", hold ? \"pause\" : \"resume\", tank), true)");
    has("the door's deploy proof is said", recovery, "step through the '{}' door together");
    has("the rest's deploy proof is said", recovery, "overseer: guild run rest {} - ");
    Check("nothing is teleported", recovery.find("TeleportTo") == std::string::npos);
    Check("and nobody is raised by hand", recovery.find("ResurrectPlayer") == std::string::npos);

    std::string const drive = Between(source, "    bool DriveGuildRun(GuildRun& run, std::time_t now)",
                                      "    void DriveGuildRunStrays(");
    std::size_t const step = drive.find("bool const regrouping = StepGuildRunRecovery(run, poll, now);");
    std::size_t const hold = drive.find("StepGuildRunHold(run, poll, ghostsWalking, now);");
    std::size_t const verdict = drive.find("OverseerDecisions::GuildRunNext(poll)");
    Check("the hold is stepped after the corpse run and before the verdict",
          step != std::string::npos && hold != std::string::npos && verdict != std::string::npos &&
              step < hold && hold < verdict);
    Check("the end of a run hands every lease back",
          Between(source, "    void EndGuildRun(", "    static bool LeaveGuildRunQueue(")
                  .find("ReturnGuildRunLeases(run, {});") != std::string::npos);
}

}  // namespace

int main()
{
    NobodyStepsThroughAlone();
    TheHealerOrAQuorumTakesThemIn();
    ALateGhostJoinsALivingGroup();
    ACasterAtThreePercentDrinksFirst();
    TheBrainHoldsWhileTheGroupRests();
    ARestCannotStallTheRun();
    TheGroupWaitsForItsGhostsBeforeItPulls();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("guild run regroup: all checks passed\n");
    return 0;
}
