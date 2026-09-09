/*
 * Whether a party flies together, or the leader is grounded so it can walk
 * together instead.
 *
 * The live failure this pins was measured on the dev realm and filed as #360.
 * The leader was aimed at the campaign dungeon and the module said:
 *
 *     '<leader>' is sent to 'at:1:...' 5547 yards away and has a departure node
 *     (37) in reach, but '<a>', '<b>', '<c>' would be left on foot behind it -
 *     walking, because a party that follows a leader across the sky arrives one
 *     cliff at a time
 *
 * and in the same second, about the walk it took instead:
 *
 *     ... and 5 of its 7 legs cross ground the other side guards, with no way
 *     round that is not farther. It walks it.
 *
 * The refusal is not wrong about the danger. It was measured with deaths under
 * #138: a leader flew 4333 yards and three followers killed themselves walking
 * straight at where he had landed. What it is wrong about is the alternative.
 * The three followers it declined to leave behind were 1500 to 4100 yards
 * behind IT, each on its own catch-up walk, each crossing guarded ground alone,
 * against level 40 guards at levels 28 to 33, on ground they had already died
 * on. Nobody was walking together. The rule paid the whole price of keeping the
 * party together and bought none of it.
 *
 * So the rule is kept and the remedy is inverted: the party flies. Every member
 * lands at the SAME arrival node, which is more together than five overland
 * walks converging on a moving leader, and the leader is grounded only for the
 * case the rule was actually written for - a member that genuinely cannot fly.
 *
 * The three questions #360 asks to have answered explicitly are answered by the
 * tests below rather than by a comment: a DEAD member does not stop the party
 * (it follows nobody), an IN COMBAT member stops it for this poll only, and a
 * STRANDED one grounds it and gets named along with the node it is missing.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::PartyFlightBlock;
using OverseerDecisions::PartyFlightBlockWord;
using OverseerDecisions::PartyFlightMember;
using OverseerDecisions::PartyFlightPlan;
using OverseerDecisions::PartyFlightVerdict;
using OverseerDecisions::PartyFlightVerdictWord;
using OverseerDecisions::PlanPartyFlight;

namespace
{

int failures = 0;

void CheckVerdict(char const* what, PartyFlightVerdict got, PartyFlightVerdict want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, PartyFlightVerdictWord(got),
                PartyFlightVerdictWord(want));
    ++failures;
}

void CheckBlock(char const* what, PartyFlightBlock got, PartyFlightBlock want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', want '%s'\n", what, PartyFlightBlockWord(got),
                PartyFlightBlockWord(want));
    ++failures;
}

void CheckName(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', want '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

void CheckNumber(char const* what, unsigned long got, unsigned long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %lu, want %lu\n", what, got, want);
    ++failures;
}

// A member with everything in order. Every test below starts from one of these
// and breaks exactly the thing it is about, so a test that fails names the one
// field it changed.
PartyFlightMember Ready(char const* name, bool leader = false)
{
    PartyFlightMember member;
    member.name = name;
    member.leader = leader;
    member.onSameMap = true;
    member.alive = true;
    member.inFlight = false;
    member.inCombat = false;
    member.atArrival = false;
    member.followingTheLeader = true;
    member.steerable = true;
    member.hasDepartureNode = true;
    member.masterInReach = true;
    member.routeKnown = true;
    member.canPayFare = true;
    member.departureNode = 37;
    member.undiscoveredNode = 0;
    return member;
}

// The party of #360: a leader with a node in reach and three followers behind
// it, all of whom can also board.
std::vector<PartyFlightMember> TheFamily()
{
    return {Ready("<leader>", true), Ready("<a>"), Ready("<b>"), Ready("<c>")};
}

// ---------------------------------------------------------------- the fix --

void APartyThatCanAllBoardFlies()
{
    PartyFlightPlan const plan = PlanPartyFlight(TheFamily());
    CheckVerdict("a whole party in reach of a flight", plan.verdict, PartyFlightVerdict::Fly);
    CheckNumber("everybody boards", plan.boarding.size(), 4);
    CheckName("and the leader is one of them", plan.boarding.empty() ? "" : plan.boarding[0],
              "<leader>");
    CheckName("nobody is blamed", plan.blockedBy, "");
}

// THIS IS THE #360 REGRESSION ITSELF. Before this change these four inputs
// produced the refusal quoted at the top of this file, and a 7720 yard walk.
void TheLeaderIsNoLongerGroundedByFollowersWhoCouldHaveFlown()
{
    std::vector<PartyFlightMember> members = TheFamily();
    // Scattered exactly as they were measured: still on the same map, still
    // alive, still on foot, and each holding its own departure node.
    members[1].departureNode = 37;
    members[2].departureNode = 25;
    members[3].departureNode = 11;
    PartyFlightPlan const plan = PlanPartyFlight(members);
    CheckVerdict("a scattered party that can all board", plan.verdict, PartyFlightVerdict::Fly);
    CheckNumber("all four board", plan.boarding.size(), 4);
}

void ALoneCharacterStillBoardsOnItsOwn()
{
    PartyFlightPlan const plan = PlanPartyFlight({Ready("<leader>", true)});
    CheckVerdict("one character with a node in reach", plan.verdict, PartyFlightVerdict::Fly);
    CheckNumber("and it is the only passenger", plan.boarding.size(), 1);
}

// ------------------------------- the three questions #360 asks explicitly --

// A GHOST FOLLOWS NOBODY, so it cannot be dragged over a cliff and it does not
// ground the party. This is the shipped #138 reading, carried over unchanged,
// and it is a test rather than a comment because it is the exemption most
// likely to be "tidied up" by somebody who has not read why it is there.
void ADeadMemberDoesNotGroundTheParty()
{
    std::vector<PartyFlightMember> members = TheFamily();
    members[2].alive = false;
    // ...and it is stranded in every other way too, to prove death is read
    // first and the rest of its state is never even asked.
    members[2].hasDepartureNode = false;
    members[2].routeKnown = false;
    members[2].canPayFare = false;
    PartyFlightPlan const plan = PlanPartyFlight(members);
    CheckVerdict("a party with one member dead", plan.verdict, PartyFlightVerdict::Fly);
    CheckNumber("and the ghost is not put on a taxi", plan.boarding.size(), 3);
}

// A FIGHT IS NOT A REFUSAL. The core will not board a character in combat, and
// the fight will be over in seconds, so the party waits a poll rather than
// committing to a walk that is measured in miles.
void AMemberInCombatHoldsTheDepartureRatherThanCancellingIt()
{
    std::vector<PartyFlightMember> members = TheFamily();
    members[1].inCombat = true;
    PartyFlightPlan const plan = PlanPartyFlight(members);
    CheckVerdict("a party with one member in a fight", plan.verdict,
                 PartyFlightVerdict::WaitForIt);
    CheckName("and it says who", plan.blockedBy, "<a>");
    CheckBlock("for the reason it is", plan.block, PartyFlightBlock::InCombat);
    CheckNumber("nobody boards while it waits", plan.boarding.size(), 0);
}

// A MEMBER THAT GENUINELY CANNOT FLY IS THE CASE THE OLD RULE WAS WRITTEN FOR,
// and it still gets the old answer.
void AStrandedMemberGroundsTheParty()
{
    std::vector<PartyFlightMember> members = TheFamily();
    members[3].hasDepartureNode = false;
    members[3].departureNode = 0;
    PartyFlightPlan const plan = PlanPartyFlight(members);
    CheckVerdict("a party with one member off the network", plan.verdict,
                 PartyFlightVerdict::Walk);
    CheckName("and it says who", plan.blockedBy, "<c>");
    CheckBlock("for the reason it is", plan.block, PartyFlightBlock::NoDepartureNode);
    CheckNumber("nobody boards", plan.boarding.size(), 0);
}

// ------------------------------------ the refusal that is worth the print --

// This is the measured case on the dev realm: the module wanted to fly node 37
// to node 80 and stopped because node 32 on that route had never been visited.
// The number is the entire actionable content of the refusal - it names a
// flight master somebody can be sent to - so it has to survive out of the
// decision and into the caller's line.
void AnUndiscoveredNodeIsNamedRatherThanSwallowed()
{
    std::vector<PartyFlightMember> members = TheFamily();
    members[2].routeKnown = false;
    members[2].undiscoveredNode = 32;
    PartyFlightPlan const plan = PlanPartyFlight(members);
    CheckVerdict("a route through a node nobody holds", plan.verdict,
                 PartyFlightVerdict::Walk);
    CheckName("names the member", plan.blockedBy, "<b>");
    CheckBlock("names the kind of refusal", plan.block, PartyFlightBlock::UndiscoveredNode);
    CheckNumber("and carries the node itself", plan.blockedNode, 32);
}

// A ROUTE THAT SIMPLY DOES NOT EXIST IS A DIFFERENT SENTENCE, because it names
// nowhere to go and no errand fixes it. Told apart by the node id being zero,
// which is the only thing separating them.
void AMissingRouteIsNotAnUndiscoveredNode()
{
    std::vector<PartyFlightMember> members = TheFamily();
    members[1].routeKnown = false;
    members[1].undiscoveredNode = 0;
    PartyFlightPlan const plan = PlanPartyFlight(members);
    CheckVerdict("no route at all", plan.verdict, PartyFlightVerdict::Walk);
    CheckBlock("is its own refusal", plan.block, PartyFlightBlock::NoRoute);
    CheckNumber("with no node to go and get", plan.blockedNode, 0);
}

void EachRemainingRefusalIsReportedAsItself()
{
    {
        std::vector<PartyFlightMember> members = TheFamily();
        members[1].masterInReach = false;
        PartyFlightPlan const plan = PlanPartyFlight(members);
        CheckVerdict("a flight master too far to walk to", plan.verdict,
                     PartyFlightVerdict::Walk);
        CheckBlock("says so", plan.block, PartyFlightBlock::MasterOutOfReach);
    }
    {
        std::vector<PartyFlightMember> members = TheFamily();
        members[2].canPayFare = false;
        PartyFlightPlan const plan = PlanPartyFlight(members);
        CheckVerdict("a member that cannot pay", plan.verdict, PartyFlightVerdict::Walk);
        CheckBlock("says so", plan.block, PartyFlightBlock::TooPoor);
        CheckName("and says who", plan.blockedBy, "<b>");
    }
    {
        // A character this module does not steer cannot be put on a taxi and
        // will follow on foot regardless, which is the whole danger. It is
        // asked before every other question about that member because none of
        // them means anything once the answer is "not ours to move".
        std::vector<PartyFlightMember> members = TheFamily();
        members[3].steerable = false;
        members[3].hasDepartureNode = false;
        PartyFlightPlan const plan = PlanPartyFlight(members);
        CheckVerdict("a member nothing here steers", plan.verdict, PartyFlightVerdict::Walk);
        CheckBlock("says that, and not the node", plan.block, PartyFlightBlock::NotSteerable);
        CheckName("and says who", plan.blockedBy, "<c>");
    }
}

// ------------------------------------------ who is not asked, and why not --

void AMemberOnAnotherMapIsNotBehindAnybody()
{
    std::vector<PartyFlightMember> members = TheFamily();
    members[1].onSameMap = false;
    members[1].hasDepartureNode = false;   // and stranded, to prove it is skipped
    PartyFlightPlan const plan = PlanPartyFlight(members);
    CheckVerdict("a member on another map", plan.verdict, PartyFlightVerdict::Fly);
    CheckNumber("does not board and does not ground", plan.boarding.size(), 3);
}

void AMemberAlreadyInTheAirIsNotOnFoot()
{
    std::vector<PartyFlightMember> members = TheFamily();
    members[2].inFlight = true;
    members[2].hasDepartureNode = false;
    PartyFlightPlan const plan = PlanPartyFlight(members);
    CheckVerdict("a member already on a taxi", plan.verdict, PartyFlightVerdict::Fly);
    CheckNumber("is not sent to buy a second ticket", plan.boarding.size(), 3);
}

// THE ONE NEW EXEMPTION (#360). A member standing at the landing has nothing to
// cross, so it is not behind anybody and does not need a ticket to where it
// already is.
void AMemberAlreadyAtTheLandingIsNotBehindAnybody()
{
    std::vector<PartyFlightMember> members = TheFamily();
    members[3].atArrival = true;
    members[3].hasDepartureNode = false;
    PartyFlightPlan const plan = PlanPartyFlight(members);
    CheckVerdict("a member already at the landing", plan.verdict, PartyFlightVerdict::Fly);
    CheckNumber("does not board", plan.boarding.size(), 3);
}

// A MEMBER WALKING ITS OWN ERRAND IS NOT FOLLOWING ANYBODY, so it cannot be
// dragged over the cliff the rule exists to prevent, and this module does not
// hijack the errand somebody else wrote for it either.
void AMemberOnItsOwnErrandIsNeitherFlownNorGroundsTheParty()
{
    std::vector<PartyFlightMember> members = TheFamily();
    members[1].followingTheLeader = false;
    members[1].hasDepartureNode = false;   // and stranded, to prove it is skipped
    PartyFlightPlan const plan = PlanPartyFlight(members);
    CheckVerdict("a member walking its own errand", plan.verdict, PartyFlightVerdict::Fly);
    CheckNumber("does not board", plan.boarding.size(), 3);
}

// ...but the LEADER is never exempted by it. The character carrying the errand
// is not following anybody by definition, and an adapter that filled that field
// in honestly for everybody would otherwise exempt the leader out of its own
// flight and fly four followers to a landing nobody is going to.
void TheLeaderIsNotExemptedByNotFollowingItself()
{
    std::vector<PartyFlightMember> members = TheFamily();
    members[0].followingTheLeader = false;
    PartyFlightPlan const plan = PlanPartyFlight(members);
    CheckVerdict("a leader that follows nobody", plan.verdict, PartyFlightVerdict::Fly);
    CheckNumber("still boards with everybody", plan.boarding.size(), 4);
    CheckName("and is still first aboard", plan.boarding.empty() ? "" : plan.boarding[0],
              "<leader>");
}

// ------------------------------------------------------- the orderings --

// WAITING OUT A FIGHT ONLY TO REFUSE AFTERWARDS IS A PARTY STANDING STILL FOR
// NOTHING. Asserted in both roster orders, because "the permanent one wins" is
// exactly the property a first-match-wins loop would appear to have and would
// silently lose the day somebody reordered the group.
void APermanentBlockerBeatsATransientOneInEitherOrder()
{
    {
        std::vector<PartyFlightMember> members = TheFamily();
        members[1].inCombat = true;
        members[2].hasDepartureNode = false;
        PartyFlightPlan const plan = PlanPartyFlight(members);
        CheckVerdict("fight first, stranded second", plan.verdict, PartyFlightVerdict::Walk);
        CheckName("and the stranded one is the news", plan.blockedBy, "<b>");
    }
    {
        std::vector<PartyFlightMember> members = TheFamily();
        members[1].hasDepartureNode = false;
        members[2].inCombat = true;
        PartyFlightPlan const plan = PlanPartyFlight(members);
        CheckVerdict("stranded first, fight second", plan.verdict, PartyFlightVerdict::Walk);
        CheckName("and the stranded one is still the news", plan.blockedBy, "<a>");
    }
}

// ...and inside ONE member too: somebody both fighting and off the network
// reports the network, because that is the half still true when the fight ends.
void AMemberThatIsBothFightingAndStrandedReportsTheStranding()
{
    std::vector<PartyFlightMember> members = TheFamily();
    members[1].inCombat = true;
    members[1].routeKnown = false;
    members[1].undiscoveredNode = 32;
    PartyFlightPlan const plan = PlanPartyFlight(members);
    CheckVerdict("both at once", plan.verdict, PartyFlightVerdict::Walk);
    CheckBlock("reports the node", plan.block, PartyFlightBlock::UndiscoveredNode);
    CheckNumber("with the number", plan.blockedNode, 32);
}

// ------------------------------------------------------------ fail closed --

void ARosterWithoutExactlyOneLeaderFliesNobody()
{
    {
        PartyFlightPlan const plan = PlanPartyFlight({Ready("<a>"), Ready("<b>")});
        CheckVerdict("no leader at all", plan.verdict, PartyFlightVerdict::Walk);
        CheckName("and blames nobody, because it is the roster", plan.blockedBy, "");
    }
    {
        PartyFlightPlan const plan =
            PlanPartyFlight({Ready("<a>", true), Ready("<b>", true)});
        CheckVerdict("two leaders", plan.verdict, PartyFlightVerdict::Walk);
        CheckName("and blames nobody", plan.blockedBy, "");
    }
    {
        PartyFlightPlan const plan = PlanPartyFlight({});
        CheckVerdict("an empty roster", plan.verdict, PartyFlightVerdict::Walk);
    }
}

// THE LEADER IS THE ONE BOARDING, so a leader that is itself exempt is not a
// question this can answer. Flying four followers to a landing the character
// carrying the errand is not going to would be #138 with the roles swapped.
void ALeaderThatIsItselfExemptFliesNobody()
{
    for (int which = 0; which < 4; ++which)
    {
        std::vector<PartyFlightMember> members = TheFamily();
        if (which == 0)
            members[0].alive = false;
        else if (which == 1)
            members[0].onSameMap = false;
        else if (which == 2)
            members[0].inFlight = true;
        else
            members[0].atArrival = true;
        PartyFlightPlan const plan = PlanPartyFlight(members);
        CheckVerdict("a leader that is not boarding", plan.verdict, PartyFlightVerdict::Walk);
        CheckNumber("takes nobody with it", plan.boarding.size(), 0);
    }
}

// ----------------------------------------------------------- properties --

// NOTHING EVER BOARDS WITHOUT ALL FOUR HALVES OF "IN REACH". Swept over every
// combination of the four, for a follower and for the leader, because this is
// the promise the executor relies on: it issues the flight for every name in
// `boarding` without re-checking any of them.
void NobodyBoardsWithoutEveryHalfOfBeingInReach()
{
    for (int bits = 0; bits < 32; ++bits)
    {
        std::vector<PartyFlightMember> members = TheFamily();
        members[1].hasDepartureNode = (bits & 1) != 0;
        members[1].masterInReach = (bits & 2) != 0;
        members[1].routeKnown = (bits & 4) != 0;
        members[1].canPayFare = (bits & 8) != 0;
        members[1].steerable = (bits & 16) != 0;
        PartyFlightPlan const plan = PlanPartyFlight(members);
        bool const ready = bits == 31;
        if (ready)
        {
            CheckVerdict("every half in place", plan.verdict, PartyFlightVerdict::Fly);
            CheckNumber("everybody boards", plan.boarding.size(), 4);
        }
        else
        {
            CheckVerdict("a half missing", plan.verdict, PartyFlightVerdict::Walk);
            CheckNumber("and nobody boards", plan.boarding.size(), 0);
        }
    }
}

// A WAIT NEVER LOSES ANYBODY. WaitForIt must never hand back a boarding list,
// because a caller that acted on one would fly half a party and leave the rest
// standing - which is the exact failure #138 measured, arrived at from the
// other direction.
void NoVerdictExceptFlyEverBoardsAnybody()
{
    for (int bits = 0; bits < 32; ++bits)
    {
        std::vector<PartyFlightMember> members = TheFamily();
        members[1].inCombat = (bits & 1) != 0;
        members[2].hasDepartureNode = (bits & 2) != 0;
        members[3].canPayFare = (bits & 4) != 0;
        members[1].alive = (bits & 8) == 0;
        members[2].onSameMap = (bits & 16) == 0;
        PartyFlightPlan const plan = PlanPartyFlight(members);
        if (plan.verdict != PartyFlightVerdict::Fly && !plan.boarding.empty())
        {
            std::printf("FAIL a %s verdict handed back %d passengers\n",
                        PartyFlightVerdictWord(plan.verdict),
                        static_cast<int>(plan.boarding.size()));
            ++failures;
        }
        if (plan.verdict == PartyFlightVerdict::Fly && plan.boarding.empty())
        {
            std::printf("FAIL a fly verdict with nobody on board\n");
            ++failures;
        }
    }
}

// EVERY REFUSAL SAYS SOMETHING DIFFERENT. A report that reads the same for two
// causes is the thing that turned "they never fly" into a year of guessing.
void EveryBlockHasItsOwnWords()
{
    PartyFlightBlock const all[] = {
        PartyFlightBlock::None,             PartyFlightBlock::InCombat,
        PartyFlightBlock::NotSteerable,     PartyFlightBlock::NoDepartureNode,
        PartyFlightBlock::MasterOutOfReach, PartyFlightBlock::UndiscoveredNode,
        PartyFlightBlock::NoRoute,          PartyFlightBlock::TooPoor,
    };
    for (PartyFlightBlock const& left : all)
    {
        for (PartyFlightBlock const& right : all)
        {
            if (&left == &right)
                continue;
            if (std::string(PartyFlightBlockWord(left)) == PartyFlightBlockWord(right))
            {
                std::printf("FAIL two blocks share the words '%s'\n",
                            PartyFlightBlockWord(left));
                ++failures;
            }
        }
    }
    PartyFlightVerdict const verdicts[] = {PartyFlightVerdict::Fly,
                                           PartyFlightVerdict::WaitForIt,
                                           PartyFlightVerdict::Walk};
    for (PartyFlightVerdict const& left : verdicts)
        for (PartyFlightVerdict const& right : verdicts)
            if (&left != &right &&
                std::string(PartyFlightVerdictWord(left)) == PartyFlightVerdictWord(right))
            {
                std::printf("FAIL two verdicts share the word '%s'\n",
                            PartyFlightVerdictWord(left));
                ++failures;
            }
}

// A BLOCKED VERDICT ALWAYS NAMES SOMEBODY, except the fail-closed roster refusals
// above, which name nobody on purpose. Stated as a property so a later refusal
// added without a name is caught here rather than in a log nobody can read.
void EveryMemberRefusalNamesAMember()
{
    std::vector<PartyFlightMember> members = TheFamily();
    bool PartyFlightMember::*const breakables[] = {
        &PartyFlightMember::steerable, &PartyFlightMember::hasDepartureNode,
        &PartyFlightMember::masterInReach, &PartyFlightMember::routeKnown,
        &PartyFlightMember::canPayFare};
    for (bool PartyFlightMember::*const field : breakables)
    {
        std::vector<PartyFlightMember> broken = members;
        broken[2].*field = false;
        PartyFlightPlan const plan = PlanPartyFlight(broken);
        CheckVerdict("a broken member", plan.verdict, PartyFlightVerdict::Walk);
        CheckName("names the member", plan.blockedBy, "<b>");
        if (plan.block == PartyFlightBlock::None)
        {
            std::printf("FAIL a refusal reported no reason\n");
            ++failures;
        }
    }
}

}  // namespace

int main()
{
    APartyThatCanAllBoardFlies();
    TheLeaderIsNoLongerGroundedByFollowersWhoCouldHaveFlown();
    ALoneCharacterStillBoardsOnItsOwn();
    ADeadMemberDoesNotGroundTheParty();
    AMemberInCombatHoldsTheDepartureRatherThanCancellingIt();
    AStrandedMemberGroundsTheParty();
    AnUndiscoveredNodeIsNamedRatherThanSwallowed();
    AMissingRouteIsNotAnUndiscoveredNode();
    EachRemainingRefusalIsReportedAsItself();
    AMemberOnAnotherMapIsNotBehindAnybody();
    AMemberAlreadyInTheAirIsNotOnFoot();
    AMemberAlreadyAtTheLandingIsNotBehindAnybody();
    AMemberOnItsOwnErrandIsNeitherFlownNorGroundsTheParty();
    TheLeaderIsNotExemptedByNotFollowingItself();
    APermanentBlockerBeatsATransientOneInEitherOrder();
    AMemberThatIsBothFightingAndStrandedReportsTheStranding();
    ARosterWithoutExactlyOneLeaderFliesNobody();
    ALeaderThatIsItselfExemptFliesNobody();
    NobodyBoardsWithoutEveryHalfOfBeingInReach();
    NoVerdictExceptFlyEverBoardsAnybody();
    EveryBlockHasItsOwnWords();
    EveryMemberRefusalNamesAMember();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a party that can all board flies together, or nobody does\n");
    return EXIT_SUCCESS;
}
