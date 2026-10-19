/*
 * Nothing clicked the portal the mage now makes (#410).
 *
 * A `cast` row makes Portal: Stormwind and reports the object; nobody walked
 * through it. `use-portal` on kind='quest' walks a character to the party's
 * portal and clicks it, the leader first, and the stranded drive's Portal rule
 * then carries the members the leader left beside it. These are the decisions
 * under both: the grammar, the default pick, the order gate, the refusals'
 * retry class, and where the Portal rule sits in DecideStrandedWay.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using OverseerDecisions::DecideStrandedWay;
using OverseerDecisions::IsPortalUseRow;
using OverseerDecisions::IsQuestUseRow;
using OverseerDecisions::ParsePortalUseRequest;
using OverseerDecisions::PickPortal;
using OverseerDecisions::PickPortalTo;
using OverseerDecisions::PortalCandidate;
using OverseerDecisions::PortalLeaderCrossesFirst;
using OverseerDecisions::PortalOrderFacts;
using OverseerDecisions::PortalUseRefusalRetry;
using OverseerDecisions::PortalUseRequest;
using OverseerDecisions::StrandedFacts;
using OverseerDecisions::StrandedWay;
using OverseerDecisions::StrandedWayExplanation;
using OverseerDecisions::StrandedWayName;
using OverseerDecisions::TownRetry;
using OverseerDecisions::TownRetryWord;
namespace Refusal = OverseerDecisions::PortalUseRefusal;

namespace
{

int failures = 0;

void Expect(bool ok, char const* what)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void CheckWay(char const* what, StrandedWay got, StrandedWay want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, StrandedWayName(got),
                StrandedWayName(want));
    ++failures;
}

// The mage's Portal: Stormwind, as #409's row reported it: type 22, owned by a
// party member, unlimited charges, sending its clicker to map 0.
PortalCandidate MagePortal(std::uint32_t guid, float yards)
{
    PortalCandidate c;
    c.guid = guid;
    c.type = OverseerDecisions::SPELLCASTER_OBJECT_TYPE;
    c.ownerInParty = true;
    c.charges = 0;
    c.yards = yards;
    c.destinationKnown = true;
    c.destinationMap = 0;
    return c;
}

void TheGrammar()
{
    Expect(IsPortalUseRow("use-portal"), "use-portal is a portal row");
    Expect(IsPortalUseRow("  use-portal 4021  "), "use-portal with a guid is a portal row");
    Expect(!IsPortalUseRow("use-gameobject 176296"), "use-gameobject is not a portal row");
    Expect(!IsQuestUseRow("use-portal"), "the quest-use verbs do not swallow use-portal");

    PortalUseRequest bare = ParsePortalUseRequest("use-portal");
    Expect(bare.ok && bare.objectGuid == 0, "a bare use-portal asks for the nearest portal");
    PortalUseRequest named = ParsePortalUseRequest("use-portal 4021");
    Expect(named.ok && named.objectGuid == 4021, "use-portal <guid> names the object");

    for (char const* bad : {"use-portal 0", "use-portal abc", "use-portal 1 2",
                            "use-portal 99999999999", "use-portal -3"})
    {
        PortalUseRequest r = ParsePortalUseRequest(bad);
        Expect(!r.ok && std::strcmp(r.error, Refusal::Malformed) == 0, bad);
    }
}

void TheDefaultPick()
{
    char const* wall = "";

    // Nearest party-owned spellcaster object, past a nearer one it may not use.
    PortalCandidate foreign = MagePortal(1, 2.f);
    foreign.ownerInParty = false;
    PortalCandidate chair = MagePortal(2, 3.f);
    chair.type = 7;  // a chair is not a spellcaster object
    PortalCandidate spent = MagePortal(3, 4.f);
    spent.charges = 1;
    spent.uses = 1;
    PortalCandidate far = MagePortal(4, 20.f);
    PortalCandidate near = MagePortal(5, 6.f);
    std::vector<PortalCandidate> all{foreign, chair, spent, far, near};
    Expect(PickPortal(all, 0, wall) == 4 && *wall == '\0',
           "the default is the nearest party-owned type 22 object with a charge left");

    // The caster itself owns the portal it made.
    PortalCandidate own = MagePortal(6, 1.f);
    own.ownerInParty = false;
    own.ownerIsSelf = true;
    all.push_back(own);
    Expect(PickPortal(all, 0, wall) == 5, "the mage may click its own portal");

    // A named object is that object, or its own wall.
    Expect(PickPortal(all, 4, wall) == 3, "a named portal is taken even when it is farther");
    Expect(PickPortal(all, 1, wall) == -1 && std::strcmp(wall, Refusal::NotInParty) == 0,
           "a named portal of another party is refused not in the owner's party");
    Expect(PickPortal(all, 3, wall) == -1 && std::strcmp(wall, Refusal::NoCharges) == 0,
           "a named portal with no charges left is refused");
    Expect(PickPortal(all, 77, wall) == -1 && std::strcmp(wall, Refusal::NoSuchObject) == 0,
           "a named portal that is not in reach is refused no such object");

    // Nothing usable: the most specific wall seen.
    Expect(PickPortal({}, 0, wall) == -1 && std::strcmp(wall, Refusal::NoSuchObject) == 0,
           "no spellcaster object in reach is no such object");
    Expect(PickPortal({foreign}, 0, wall) == -1 && std::strcmp(wall, Refusal::NotInParty) == 0,
           "only another party's portal in reach is not in the owner's party");
    Expect(PickPortal({foreign, spent}, 0, wall) == -1 &&
               std::strcmp(wall, Refusal::NoCharges) == 0,
           "the party's own spent portal is named over another party's");
}

void TheOrderGate()
{
    PortalOrderFacts f;
    f.grouped = true;
    f.leadsGroup = false;
    f.leaderReadable = true;
    f.leaderMap = 1;
    f.portalMap = 1;
    Expect(PortalLeaderCrossesFirst(f),
           "a follower whose leader still stands on the portal's map waits for the leader");

    PortalOrderFacts leader = f;
    leader.leadsGroup = true;
    Expect(!PortalLeaderCrossesFirst(leader), "the leader is never told to wait for itself");

    PortalOrderFacts crossed = f;
    crossed.leaderMap = 0;
    Expect(!PortalLeaderCrossesFirst(crossed), "a follower may cross once its leader has");

    PortalOrderFacts alone = f;
    alone.grouped = false;
    Expect(!PortalLeaderCrossesFirst(alone), "a character in no group has no leader to wait on");

    PortalOrderFacts unread = f;
    unread.leaderReadable = false;
    Expect(!PortalLeaderCrossesFirst(unread), "a leader that cannot be read is not waited on");

    Expect(PortalUseRefusalRetry(Refusal::LeaderFirst) == TownRetry::Later &&
               std::strcmp(TownRetryWord(PortalUseRefusalRetry(Refusal::LeaderFirst)),
                           "later") == 0,
           "the party leader crosses first is refused later");
    Expect(PortalUseRefusalRetry(Refusal::NotInParty) == TownRetry::Never,
           "another party's portal never becomes this party's");
    Expect(PortalUseRefusalRetry(Refusal::NoCharges) == TownRetry::Elsewhere,
           "a spent portal is answered by another one");
    Expect(PortalUseRefusalRetry(OverseerDecisions::CastRefusal::InCombat) == TownRetry::Later,
           "the refusals shared with cast keep cast's retry class");
}

// The follower the leader left beside the portal: on map 1, its leader on map
// 0, bound on the leader's side with the stone ready, and a boat serving both.
// Before #410, rule 6 hearthed it; that is still the answer with no portal.
StrandedFacts LeftBeside()
{
    StrandedFacts f;
    f.readable = true;
    f.alive = true;
    f.memberMap = 1;
    f.leaderMap = 0;
    f.carriesStone = true;
    f.stoneReady = true;
    f.boundOnLeaderMap = true;
    f.transportServes = true;
    return f;
}

void ThePortalRule()
{
    StrandedFacts with = LeftBeside();
    with.portalToLeaderMap =
        PickPortalTo({MagePortal(9, 5.f)}, with.leaderMap) >= 0;
    CheckWay("a party portal to the leader's map comes ahead of the hearth",
             DecideStrandedWay(with), StrandedWay::Portal);

    StrandedFacts sailing = with;
    sailing.boundOnLeaderMap = false;
    CheckWay("a party portal to the leader's map comes ahead of the boat",
             DecideStrandedWay(sailing), StrandedWay::Portal);

    PortalCandidate elsewhere = MagePortal(9, 5.f);
    elsewhere.destinationMap = 530;
    PortalCandidate unknown = MagePortal(10, 4.f);
    unknown.destinationKnown = false;
    StrandedFacts other = LeftBeside();
    other.portalToLeaderMap = PickPortalTo({elsewhere, unknown}, other.leaderMap) >= 0;
    Expect(!other.portalToLeaderMap, "a portal to any other map is not a way to the leader");
    CheckWay("a portal to any other map is ignored", DecideStrandedWay(other),
             StrandedWay::Hearth);

    PortalCandidate foreign = MagePortal(11, 3.f);
    foreign.ownerInParty = false;
    Expect(PickPortalTo({foreign}, 0) < 0, "another party's portal is not this member's way");
    Expect(PickPortalTo({elsewhere, MagePortal(12, 8.f), MagePortal(13, 6.f)}, 0) == 2,
           "the nearest portal to the leader's map is the one walked to");

    CheckWay("with no portal the hearth still answers", DecideStrandedWay(LeftBeside()),
             StrandedWay::Hearth);
    StrandedFacts boat = LeftBeside();
    boat.boundOnLeaderMap = false;
    CheckWay("with no portal the boat still answers", DecideStrandedWay(boat),
             StrandedWay::Sail);

    StrandedFacts dead = with;
    dead.alive = false;
    CheckWay("a dead member waits even beside a portal", DecideStrandedWay(dead),
             StrandedWay::Wait);
    StrandedFacts fighting = with;
    fighting.inCombat = true;
    CheckWay("a fighting member waits even beside a portal", DecideStrandedWay(fighting),
             StrandedWay::Wait);
    StrandedFacts family = with;
    family.familyOwnsTheWay = true;
    CheckWay("the family's own way still stands the portal rule down",
             DecideStrandedWay(family), StrandedWay::StandDown);
    StrandedFacts there = with;
    there.memberMap = 0;
    CheckWay("a member already on its leader's map is not stranded", DecideStrandedWay(there),
             StrandedWay::NotStranded);

    std::string const said = StrandedWayExplanation(with, StrandedWay::Portal);
    Expect(said.find("map 1") != std::string::npos && said.find("map 0") != std::string::npos,
           "the portal line names both maps");
    Expect(std::strcmp(StrandedWayName(StrandedWay::Portal), "portal") == 0,
           "the way is said as portal");
}

}  // namespace

int main()
{
    TheGrammar();
    TheDefaultPick();
    TheOrderGate();
    ThePortalRule();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("use-portal picks the party's portal, waits for the leader, and the "
                "stranded drive carries the rest through it\n");
    return 0;
}
