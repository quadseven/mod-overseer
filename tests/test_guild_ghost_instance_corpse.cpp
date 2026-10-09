/*
 * A natural guild member whose corpse lies inside a dungeon runs back to the
 * dungeon's entrance and walks in, as a player does; where no walk works it
 * takes the spirit healer. Before this, the guild ghost drive read
 * Player::GetCorpse()'s null (the lookup is scoped to the ghost's own map) as
 * "not released yet" and skipped the ghost on every poll: measured read-only on
 * the dev realm 2026-10-09, 17 members ghosts for 30 to 300 minutes at a
 * dungeon's graveyard, and 253 of 256 dungeon deaths in 48 hours unrecovered.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::DecideInstanceGhost;
using OverseerDecisions::DecideRevivedInside;
using OverseerDecisions::GhostLeg;
using OverseerDecisions::GhostWalkLegs;
using OverseerDecisions::InstanceGhostFacts;
using OverseerDecisions::InstanceGhostLimits;
using OverseerDecisions::InstanceGhostReason;
using OverseerDecisions::InstanceGhostStep;
using OverseerDecisions::RevivedInsideFacts;
using OverseerDecisions::RevivedInsideStep;

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

// The measured case: a member released out of the Deadmines to the graveyard
// outside, its corpse on map 36, the entrance (areatrigger 78) on map 0.
InstanceGhostFacts DeadminesGhost()
{
    InstanceGhostFacts facts;
    facts.released = true;
    facts.corpseElsewhere = true;
    facts.corpseInDungeon = true;
    facts.doorOnThisMap = true;
    facts.healerKnown = true;
    return facts;
}

void TheRunBack()
{
    InstanceGhostLimits const limits;
    InstanceGhostFacts facts = DeadminesGhost();
    Check("a ghost whose corpse is inside a dungeon walks to its entrance",
          DecideInstanceGhost(facts, limits).step == InstanceGhostStep::WalkToDoor);
    Check("and says the door is ahead",
          DecideInstanceGhost(facts, limits).reason == InstanceGhostReason::DoorAhead);

    facts.inDoor = true;
    Check("standing in the entrance, it knocks",
          DecideInstanceGhost(facts, limits).step == InstanceGhostStep::Knock);

    facts = DeadminesGhost();
    facts.walkSeconds = limits.walkSeconds;
    facts.stalledSeconds = limits.stallSeconds;
    Check("at both clocks' edges it still walks",
          DecideInstanceGhost(facts, limits).step == InstanceGhostStep::WalkToDoor);
}

void NotThisDrivesGhost()
{
    InstanceGhostFacts facts = DeadminesGhost();
    facts.released = false;
    Check("a body that has not released is the dead engine's",
          DecideInstanceGhost(facts).step == InstanceGhostStep::NotMine);
    facts = DeadminesGhost();
    facts.corpseElsewhere = false;
    Check("a corpse on the ghost's own map is the ordinary drive's",
          DecideInstanceGhost(facts).step == InstanceGhostStep::NotMine);
    Check("and says why",
          DecideInstanceGhost(facts).reason == InstanceGhostReason::CorpseHere);
}

void NoWalkWorks()
{
    InstanceGhostLimits const limits;
    struct Case
    {
        char const* what;
        void (*change)(InstanceGhostFacts&);
        InstanceGhostReason reason;
    };
    Case const cases[] = {
        {"no entrance on this map takes the spirit healer",
         [](InstanceGhostFacts& f) { f.doorOnThisMap = false; }, InstanceGhostReason::NoDoorHere},
        {"a corpse on a map that is not a dungeon takes the spirit healer",
         [](InstanceGhostFacts& f) { f.corpseInDungeon = false; },
         InstanceGhostReason::NotADungeon},
        {"a walk that stopped getting closer takes the spirit healer",
         [](InstanceGhostFacts& f) { f.stalledSeconds = 121; }, InstanceGhostReason::WalkStalled},
        {"a walk out of time takes the spirit healer",
         [](InstanceGhostFacts& f) { f.walkSeconds = 601; }, InstanceGhostReason::WalkSpent},
        {"three refused knocks take the spirit healer, even standing in the door",
         [](InstanceGhostFacts& f) {
             f.refusedKnocks = 3;
             f.inDoor = true;
         },
         InstanceGhostReason::DoorRefused},
        {"the spirit healer once chosen stays chosen, even back in the door",
         [](InstanceGhostFacts& f) {
             f.choseHealer = true;
             f.inDoor = true;
         },
         InstanceGhostReason::HealerChosen},
    };
    for (Case const& c : cases)
    {
        InstanceGhostFacts facts = DeadminesGhost();
        c.change(facts);
        OverseerDecisions::InstanceGhostVerdict const verdict = DecideInstanceGhost(facts, limits);
        Check(c.what, verdict.step == InstanceGhostStep::SpiritHealer && verdict.reason == c.reason);
        facts.healerKnown = false;
        Check(c.what, DecideInstanceGhost(facts, limits).step == InstanceGhostStep::Stranded);
    }

    InstanceGhostFacts facts = DeadminesGhost();
    facts.refusedKnocks = 2;
    facts.inDoor = true;
    Check("two refused knocks still knock",
          DecideInstanceGhost(facts, limits).step == InstanceGhostStep::Knock);
}

void TheWords()
{
    Check("walk word", std::string(OverseerDecisions::InstanceGhostStepWord(
                           InstanceGhostStep::WalkToDoor)) == "walk_to_door");
    Check("healer word", std::string(OverseerDecisions::InstanceGhostStepWord(
                             InstanceGhostStep::SpiritHealer)) == "spirit_healer");
    Check("reason text", std::string(OverseerDecisions::InstanceGhostReasonText(
                             InstanceGhostReason::WalkStalled))
                                 .find("stopped getting closer") != std::string::npos);
    Check("revived word", std::string(OverseerDecisions::RevivedInsideStepWord(
                              RevivedInsideStep::GiveUp)) == "give_up");
}

bool Near(float a, float b)
{
    return std::fabs(a - b) < 0.01f;
}

void TheLegs()
{
    // The Deadmines graveyard to areatrigger 78: about 822 yards, so three
    // points along the line and not the door.
    float const fromX = -10547.f;
    float const fromY = 1197.f;
    float const doorX = -11208.5f;
    float const doorY = 1685.3f;
    std::vector<GhostLeg> legs = GhostWalkLegs(fromX, fromY, doorX, doorY, 250.f, 120.f);
    Check("a far door gives three legs", legs.size() == 3);
    float const straight = std::hypot(doorX - fromX, doorY - fromY);
    float const want[] = {120.f, 80.f, 40.f};
    for (std::size_t i = 0; i < legs.size() && i < 3; ++i)
    {
        float const along = std::hypot(legs[i].x - fromX, legs[i].y - fromY);
        float const toDoor = std::hypot(doorX - legs[i].x, doorY - legs[i].y);
        Check("each leg is its share of 120 yards", Near(along, want[i]));
        Check("on the line to the door", Near(along + toDoor, straight));
        Check("and is not the door", !legs[i].arrives);
    }

    legs = GhostWalkLegs(0.f, 0.f, 100.f, 0.f, 250.f, 120.f);
    Check("a door within reach is tried first", !legs.empty() && legs[0].arrives &&
                                                    Near(legs[0].x, 100.f));
    Check("then the legs short of it", legs.size() == 3 && Near(legs[1].x, 80.f) &&
                                           Near(legs[2].x, 40.f));

    legs = GhostWalkLegs(5.f, 5.f, 5.f, 5.f, 250.f, 120.f);
    Check("standing on it, the door is the only leg", legs.size() == 1 && legs[0].arrives);
}

void AfterTheRunBack()
{
    RevivedInsideFacts facts;
    facts.exitKnown = true;
    Check("raised inside alone, it walks to the exit",
          DecideRevivedInside(facts, 180) == RevivedInsideStep::Walk);
    facts.inExit = true;
    Check("in the exit, it knocks", DecideRevivedInside(facts, 180) == RevivedInsideStep::Knock);
    facts.inCombat = true;
    Check("in combat, it holds", DecideRevivedInside(facts, 180) == RevivedInsideStep::Hold);
    facts.partyAliveHere = true;
    Check("with its party alive inside, it rejoins",
          DecideRevivedInside(facts, 180) == RevivedInsideStep::Rejoin);
    facts = RevivedInsideFacts{};
    Check("no exit known gives up", DecideRevivedInside(facts, 180) == RevivedInsideStep::GiveUp);
    facts.exitKnown = true;
    facts.seconds = 181;
    Check("past the budget gives up", DecideRevivedInside(facts, 180) == RevivedInsideStep::GiveUp);
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string const source = buffer.str();
    Check("the module source is readable (run from the repo root)", !source.empty());
    std::size_t const drive = source.find("void DriveGuildGhostRecovery()");
    std::size_t const end = source.find("bool LeaveGuildDeathSpot(", drive);
    Check("the guild ghost drive is where it was", drive != std::string::npos &&
                                                       end != std::string::npos);
    std::string const body =
        drive != std::string::npos && end != std::string::npos ? source.substr(drive, end - drive)
                                                               : std::string();
    Check("a null GetCorpse() is no longer skipped as 'not released yet'",
          body.find("continue;  // not released yet") == std::string::npos);
    Check("it goes to the instance corpse drive",
          body.find("DriveInstanceCorpseGhost(bot, botAI, name, now);") != std::string::npos);
    Check("the corpse is read where the core keeps it across maps",
          source.find("bot->GetCorpseLocation()") != std::string::npos);
    Check("the drive asks the decision",
          source.find("OverseerDecisions::DecideInstanceGhost(") != std::string::npos);
    Check("a member raised inside asks the decision",
          source.find("OverseerDecisions::DecideRevivedInside(") != std::string::npos);
    Check("the walk is taken a routed leg at a time",
          source.find("OverseerDecisions::GhostWalkLegs(") != std::string::npos);
    Check("the deployed binary can be proven by its log line",
          source.find("instance corpse run-back") != std::string::npos);
}

}  // namespace

int main()
{
    TheRunBack();
    NotThisDrivesGhost();
    NoWalkWorks();
    TheWords();
    TheLegs();
    AfterTheRunBack();
    TheAdapterIsWired();
    if (failures)
        return 1;
    std::printf("ok test_guild_ghost_instance_corpse\n");
    return 0;
}
