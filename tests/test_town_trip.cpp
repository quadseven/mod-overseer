/*
 * The town trip that is owed by a need rather than by a dungeon phase.
 *
 * WHAT THIS PINS, MEASURED RATHER THAN IMAGINED. Every command the realm has
 * ever run, grouped by outcome. sell: 17200 "vendor not in range" against 1608
 * delivered, with the remaining errors about the seller rather than the counter
 * - 4395 not online, 883 item not carried, 415 dead, 104 in flight. repair: 47
 * "repairer not in range" against 15 delivered, 3 "nothing is damaged" and 2 not
 * online. The auction leg has never written a row at all, and no flight node has
 * ever been learned.
 *
 * Those are one fault and not four. The executors sweep the creatures around
 * wherever the character is already standing; none of them is a walker. A live
 * test settles it: with a repair vendor 47 yards off, four repair commands all
 * refused inside one second.
 *
 * AND ONE MEASUREMENT THAT WAS WRONG, PINNED HERE SO IT IS NOT TAKEN AGAIN. The
 * first reading said the family carried three or four EQUIPPED items at
 * durability zero each. It counted items that cannot hold durability at all -
 * shirts, rings, amulets, trinkets, most cloaks - which read zero for ever.
 * Every item of the leader's that HAS durability is at full. So the trip's
 * acceptance is asked as "nothing with a maximum is below it", which can pass,
 * rather than "nothing reads zero", which never can.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::CounterRole;
using OverseerDecisions::PlanTownTrip;
using OverseerDecisions::ProveTownTrip;
using OverseerDecisions::TownNeed;
using OverseerDecisions::TownReason;
using OverseerDecisions::TownReasonOpensATrip;
using OverseerDecisions::TownReasonWord;
using OverseerDecisions::TownStop;
using OverseerDecisions::TownStopFacts;
using OverseerDecisions::TownStopWord;
using OverseerDecisions::TownTripFormation;
using OverseerDecisions::TownTripFormationFor;
using OverseerDecisions::TownTripLimits;
using OverseerDecisions::TownTripMemberAccepted;
using OverseerDecisions::TownTripMemberReason;
using OverseerDecisions::TownTripMemberStop;
using OverseerDecisions::TownTripPlan;
using OverseerDecisions::TownTripProof;
using OverseerDecisions::TownTripProofWord;
using OverseerDecisions::TownTripRoleFor;

namespace
{

int failures = 0;

void Fail(char const* what, std::string const& wanted, std::string const& got)
{
    std::printf("FAIL %s: wanted '%s', got '%s'\n", what, wanted.c_str(), got.c_str());
    ++failures;
}

char const* RoleName(CounterRole role)
{
    switch (role)
    {
        case CounterRole::None:       return "none";
        case CounterRole::Vendor:     return "vendor";
        case CounterRole::Banker:     return "banker";
        case CounterRole::Repairer:   return "repairer";
        case CounterRole::Auctioneer: return "auctioneer";
    }
    return "unknown";
}

char const* FormationName(TownTripFormation formation)
{
    return formation == TownTripFormation::Together ? "together" : "left behind";
}

TownTripLimits Limits()
{
    // The shipped defaults, named here rather than relied on implicitly so that
    // a test reads as a statement about numbers a reader can see.
    TownTripLimits limits;
    limits.freeBagSlotsToGo = 3;
    limits.brokenToGo = 1;
    limits.cooldownSeconds = 900;
    limits.boundSeconds = 1200;
    return limits;
}

TownNeed Need(bool present, unsigned damaged, unsigned broken, unsigned freeSlots)
{
    TownNeed need;
    need.present = present;
    need.damagedItems = damaged;
    need.brokenItems = broken;
    need.freeBagSlots = freeSlots;
    return need;
}

// The five characters as the corrected query actually reads them tonight:
// nothing damaged, and bags from full to roomy.
TownNeed Tonight(unsigned freeSlots)
{
    return Need(true, 0, 0, freeSlots);
}

void Reason(char const* what, TownNeed const& need, TownReason want)
{
    TownReason const got = TownTripMemberReason(need, Limits());
    if (got != want)
        Fail(what, TownReasonWord(want), TownReasonWord(got));
}

void Stop(char const* what, TownStopFacts const& facts, TownStop want)
{
    TownStop const got = TownTripMemberStop(facts);
    if (got != want)
        Fail(what, TownStopWord(want), TownStopWord(got));
}

void Proof(char const* what, TownNeed const& before, TownNeed const& after,
           std::uint64_t copperBefore, std::uint64_t copperAfter, TownTripProof want)
{
    TownTripProof const got = ProveTownTrip(before, after, copperBefore, copperAfter);
    if (got != want)
        Fail(what, TownTripProofWord(want), TownTripProofWord(got));
}

void Check(char const* what, bool got, bool want)
{
    if (got != want)
        Fail(what, want ? "true" : "false", got ? "true" : "false");
}

// ------------------------------------------------ why a member wants a town --

// The measurement that was corrected. Nine items at full and four that cannot
// wear at all is a character with NOTHING to do at a repairer, and reading it
// any other way is what sent a family to town for a shirt.
void FullGearOnRoomyBagsWantsNothing()
{
    Reason("everything at full, 27 free slots", Tonight(27), TownReason::None);
}

// ...AND THE ITEMS THAT CANNOT WEAR NEVER REACH THIS DECISION. The adapter drops
// them on a zero maximum, so a member whose only zeroes are a shirt and three
// rings arrives here as the undamaged character it is.
void ItemsThatCannotWearAreNotDamage()
{
    Reason("four rings and a shirt, counted correctly as nothing",
           Need(true, 0, 0, 20), TownReason::None);
}

// The live problem: one character at zero free slots of 46, a second at five of
// 62. Bags are a reason to go in their own right (#363).
void FullBagsAreAReasonToGo()
{
    Reason("no free slots at all", Tonight(0), TownReason::Bags);
    Reason("three free slots, at the floor", Tonight(3), TownReason::Bags);
    Reason("four free slots, above it", Tonight(4), TownReason::None);
}

// Broken gear outranks full bags, because a slot giving no armour is a worse
// state than a bag that cannot take another green.
void BrokenGearOutranksFullBags()
{
    Reason("broken and full", Need(true, 3, 1, 0), TownReason::Broken);
}

// Worn is a real reading and the weakest one.
void WornIsTheWeakestReason()
{
    Reason("scuffed, roomy bags", Need(true, 4, 0, 30), TownReason::Worn);
}

// A member nobody can read has no reason at all, which is not the same as having
// no need. Opening a trip for a character that is logged out would be this
// module acting on a reading it never took.
void AMemberNobodyCanReadHasNoReason()
{
    Reason("logged out, and its bags read as empty because nothing was read",
           Need(false, 0, 0, 0), TownReason::None);
}

// ---------------------------------------- and which of them opens a trip --

// THE WHOLE OF THE TRIGGER DECISION. Durability falls on every hit taken, so a
// family that goes to town whenever anything is below maximum is a family
// permanently in town.
void WornAloneNeverOpensATrip()
{
    Check("worn opens a trip", TownReasonOpensATrip(TownReason::Worn), false);
    Check("none opens a trip", TownReasonOpensATrip(TownReason::None), false);
    Check("bags opens a trip", TownReasonOpensATrip(TownReason::Bags), true);
    Check("broken opens a trip", TownReasonOpensATrip(TownReason::Broken), true);
}

void EachReasonHasACounterOrNone()
{
    struct Case { TownReason reason; CounterRole role; };
    Case const cases[] = {
        {TownReason::Broken, CounterRole::Repairer},
        {TownReason::Bags,   CounterRole::Vendor},
        {TownReason::Worn,   CounterRole::None},
        {TownReason::None,   CounterRole::None},
    };
    for (Case const& c : cases)
    {
        CounterRole const got = TownTripRoleFor(c.reason);
        if (got != c.role)
            Fail(TownReasonWord(c.reason), RoleName(c.role), RoleName(got));
    }
}

void EveryReasonHasAWord()
{
    TownReason const all[] = {TownReason::None, TownReason::Worn, TownReason::Bags,
                              TownReason::Broken};
    for (TownReason reason : all)
        if (std::string(TownReasonWord(reason)) == "unknown")
            Fail("a reason with no word", "a word", "unknown");
}

// ------------------------------------------------ the party-level decision --

// The family as it stands tonight, bag by bag: 0 free of 46, 5 of 62, 10 of 62,
// 9 of 48, 27 of 64. One character cannot loot at all, and that one character is
// enough - the whole family goes, to a vendor.
void OneCharacterThatCannotLootTakesTheWholeFamily()
{
    std::vector<TownNeed> const family = {Tonight(0), Tonight(5), Tonight(10),
                                          Tonight(9), Tonight(27)};
    TownTripPlan const plan = PlanTownTrip(family, Limits(), 3600, false);
    Check("the family goes", plan.go, true);
    if (plan.role != CounterRole::Vendor)
        Fail("the counter", "vendor", RoleName(plan.role));
    if (plan.membersDriving != 1)
        Fail("members driving the trip", "1", std::to_string(plan.membersDriving));
    if (plan.membersOwed != 1)
        Fail("members owed anything at all", "1", std::to_string(plan.membersOwed));
}

// A single broken member turns the whole trip into a repair trip, and the four
// who only want a vendor come along - which costs them nothing, because a repair
// vendor carries the vendor flag too.
void OneBrokenMemberSetsTheDestinationForEverybody()
{
    std::vector<TownNeed> family = {Tonight(0), Tonight(1), Tonight(2), Tonight(3),
                                    Need(true, 5, 2, 30)};
    TownTripPlan const plan = PlanTownTrip(family, Limits(), 3600, false);
    Check("the family goes", plan.go, true);
    if (plan.role != CounterRole::Repairer)
        Fail("the counter", "repairer", RoleName(plan.role));
    if (plan.membersDriving != 5)
        Fail("members driving the trip", "5", std::to_string(plan.membersDriving));
}

// A family of merely worn characters does not go anywhere, and the count of who
// wants something is still reported - a reading, not a decision.
void AWornFamilyStaysWhereItIs()
{
    std::vector<TownNeed> const family = {Need(true, 2, 0, 20), Need(true, 6, 0, 30)};
    TownTripPlan const plan = PlanTownTrip(family, Limits(), 3600, false);
    Check("a worn family goes", plan.go, false);
    if (plan.membersOwed != 2)
        Fail("members owed", "2", std::to_string(plan.membersOwed));
    if (plan.membersDriving != 0)
        Fail("members driving", "0", std::to_string(plan.membersDriving));
    if (plan.reason != TownReason::None)
        Fail("the reason of a trip nobody takes", "none", TownReasonWord(plan.reason));
}

// THE COOLDOWN IS THE BRAKE ON A TRIP THAT CANNOT WORK. A family whose need is
// unmeetable - no vendor of that role on this map, a bridge writing no rows -
// would otherwise walk to town on every poll, for ever.
void ATripJustTakenIsNotTakenAgain()
{
    std::vector<TownNeed> const family = {Tonight(0)};
    Check("goes again eight seconds later",
          PlanTownTrip(family, Limits(), 8, false).go, false);
    Check("goes again fourteen minutes later",
          PlanTownTrip(family, Limits(), 840, false).go, false);
    Check("goes again sixteen minutes later",
          PlanTownTrip(family, Limits(), 960, false).go, true);
}

// A COOLDOWN OF ZERO IS NOT A DISABLED COOLDOWN. It is one that has always
// expired.
void ACooldownOfZeroLetsEveryTripThrough()
{
    TownTripLimits limits = Limits();
    limits.cooldownSeconds = 0;
    std::vector<TownNeed> const family = {Tonight(0)};
    Check("a zero cooldown lets the next poll go",
          PlanTownTrip(family, limits, 0, false).go, true);
}

// SOMEBODY ELSE OWNS THE PARTY. A run in any phase decides where every member
// should be, and a town trip that started under one would be two drives steering
// five characters.
void NobodyGoesToTownWhileSomethingElseOwnsTheParty()
{
    std::vector<TownNeed> const family = {Tonight(0), Need(true, 9, 4, 0)};
    Check("goes while busy", PlanTownTrip(family, Limits(), 3600, true).go, false);
}

// An empty roster is not a trip, and is answered rather than left to a loop that
// happens to do nothing.
void AnEmptyRosterIsNotATrip()
{
    Check("an empty family goes",
          PlanTownTrip(std::vector<TownNeed>(), Limits(), 3600, false).go, false);
}

// --------------------------------------------------- one destination --

// A town trip is not a crossing. There is no navmesh across an ocean, so a
// member on another map is left where it is rather than aimed at a coordinate it
// cannot path to.
void AMemberOnAnotherMapIsLeftWhereItIs()
{
    TownTripFormation const together = TownTripFormationFor(1, 1);
    if (together != TownTripFormation::Together)
        Fail("same map", "together", FormationName(together));
    TownTripFormation const apart = TownTripFormationFor(0, 1);
    if (apart != TownTripFormation::LeftBehind)
        Fail("across the ocean", "left behind", FormationName(apart));
}

// -------------------------------------- what happens to one member, one poll --

constexpr float WALK_LIMIT = 1500.f;

TownStopFacts Facts(bool present, bool owed, bool atTheCounter, bool isLeader,
                    bool leaderAtTheCounter)
{
    TownStopFacts facts;
    facts.present = present;
    facts.owed = owed;
    facts.atTheCounter = atTheCounter;
    facts.isLeader = isLeader;
    facts.leaderAtTheCounter = leaderAtTheCounter;
    // A follower a few yards from an arrived leader, under the adapter's own
    // limit (TRAVEL_FLIGHT_MIN_YARDS), which is the case every test below
    // this one was written about.
    facts.yardsFromLeader = 40.f;
    facts.walkLimitYards = WALK_LIMIT;
    return facts;
}

// THE MEASUREMENT THE WALK LIMIT EXISTS FOR (2026-09-23). The leader stood at
// a vendor near Cenarion Hold in Silithus; four members were in Winterspring,
// 11,000 to 12,500 yards away. Each was escorted "to vendor under its own
// power", and one died twice to Hederine elites on the way. A walk of eleven
// thousand yards is a journey, not the last few yards.
void AFollowerAContinentAwayIsNotWalkedToTheCounter()
{
    TownStopFacts far = Facts(true, true, false, false, true);
    far.yardsFromLeader = 11505.f;
    Stop("a follower 11505 yards from an arrived leader", far, TownStop::TooFar);

    TownStopFacts edge = Facts(true, true, false, false, true);
    edge.yardsFromLeader = WALK_LIMIT;
    Stop("a follower exactly at the limit", edge, TownStop::Walk);
    edge.yardsFromLeader = WALK_LIMIT + 1.f;
    Stop("a follower one yard past it", edge, TownStop::TooFar);

    // THE LEADER IS NEVER REFUSED BY IT. Its distance from itself is zero, and
    // the leader's walk is the journey to the counter it resolved from where
    // it stands.
    TownStopFacts leader = Facts(true, true, false, true, false);
    leader.yardsFromLeader = 11505.f;
    Stop("the leader, whatever the reading", leader, TownStop::Walk);

    // AND IT DOES NOT TOUCH THE JOURNEY. Mid-journey a follower is not aimed
    // by this trip at all, so there is no walk to refuse.
    TownStopFacts behind = Facts(true, true, false, false, false);
    behind.yardsFromLeader = 11505.f;
    Stop("a far follower mid-journey still follows", behind, TownStop::Follow);

    // A far follower that is somehow at the counter still trades.
    TownStopFacts lucky = Facts(true, true, true, false, true);
    lucky.yardsFromLeader = 11505.f;
    Stop("a far follower in reach of a counter trades", lucky, TownStop::Trade);

    // A limit nobody set refuses rather than allows.
    TownStopFacts unset = Facts(true, true, false, false, true);
    unset.walkLimitYards = 0.f;
    Stop("an unset limit refuses the walk", unset, TownStop::TooFar);
}

// THE #298 STAND-DOWN, WHICH THE TRIP'S WALK TOOK OVER AND DID NOT HONOR. Og
// died at 06:35:49 on the walk to a vendor and again at 06:38:02, because the
// trip took the catch-up walk over and re-sent it the moment the revival hold
// let go. A member inside its stand-down is not walked, however near.
void AMemberInsideItsStandDownIsNotWalked()
{
    TownStopFacts dead = Facts(true, true, false, false, true);
    dead.stoodDown = true;
    Stop("a stood-down follower, a few yards off", dead, TownStop::StoodDown);
    dead.yardsFromLeader = 11505.f;
    Stop("a stood-down follower, far away", dead, TownStop::StoodDown);

    TownStopFacts trading = Facts(true, true, true, false, true);
    trading.stoodDown = true;
    Stop("a stood-down follower already at the counter trades", trading,
         TownStop::Trade);
}

// THE MEASUREMENT THIS RULE EXISTS FOR. Aiming the leader at a vendor 154 yards
// away worked and the other four ended up 945 to 2184 yards behind. So on the
// journey the leader is the only character this trip aims, and the family is
// brought by the catch-up walk and the regroup wait that already exist (#404).
void OnlyTheLeaderIsAimedOnTheJourney()
{
    Stop("the leader, mid-journey", Facts(true, true, false, true, false),
         TownStop::Walk);
    Stop("a follower, mid-journey", Facts(true, true, false, false, false),
         TownStop::Follow);
}

// AND ONCE THE LEADER IS THERE, EVERYBODY IS AIMED. The catch-up hands back at a
// distance far wider than the five and a half yards the core will trade from, so
// the last few yards are a walk this trip has to ask for.
void EverybodyWalksTheLastFewYards()
{
    Stop("a follower once the party has arrived",
         Facts(true, true, false, false, true), TownStop::Walk);
}

// A FOLLOWER THAT GOT LUCKY SPENDS THE POLL IT HAS. Trade is tested above
// Follow, so a member that wandered into reach of the counter during the journey
// transacts rather than being told to keep following.
void AFollowerAlreadyAtTheCounterTradesAnyway()
{
    Stop("a follower in reach before the leader arrived",
         Facts(true, true, true, false, false), TownStop::Trade);
}

// The three tests this shares with the repair leg, unchanged and re-pinned here
// because this is the function that runs now.
void AnUnreadableMemberIsWaitedForRatherThanFinished()
{
    Stop("logged out mid-trip", Facts(false, true, false, false, true),
         TownStop::Wait);
    Stop("logged out and owing nothing", Facts(false, false, true, true, true),
         TownStop::Wait);
}

void AMemberThatWantsNothingIsFinishedWhereverItStands()
{
    Stop("nothing owed, nowhere near", Facts(true, false, false, false, true),
         TownStop::Done);
    Stop("nothing owed, standing at the counter", Facts(true, false, true, true, true),
         TownStop::Done);
}

void TheCounterOutranksTheWalk()
{
    Stop("the leader in reach", Facts(true, true, true, true, true), TownStop::Trade);
}

void EveryCombinationHasAnAnswer()
{
    for (int i = 0; i < 128; ++i)
    {
        TownStopFacts facts = Facts((i & 1) != 0, (i & 2) != 0, (i & 4) != 0,
                                    (i & 8) != 0, (i & 16) != 0);
        facts.stoodDown = (i & 32) != 0;
        facts.yardsFromLeader = (i & 64) != 0 ? 11505.f : 40.f;
        if (std::string(TownStopWord(TownTripMemberStop(facts))) == "unknown")
            Fail("a combination with no answer", "a word", "unknown");
    }
}

// -------------------------------------------------- how long a visit lasts --

// THE ASYMMETRY THIS FUNCTION IS FOR. A repair trip knows when it is done,
// because this module makes the repair. A selling trip cannot know, because the
// executor deliberately never chooses the item - that rule lives in the bridge,
// where all five bag lists are in one place. So a visit is judged on time.
void AVisitThatHasNotStartedIsNotAVisit()
{
    if (OverseerDecisions::TownVisitStep(false, 900, 60) !=
        OverseerDecisions::TownVisit::Travelling)
        Fail("still walking", "travelling",
             OverseerDecisions::TownVisitWord(
                 OverseerDecisions::TownVisitStep(false, 900, 60)));
}

void AVisitEndsWhenItHasBeenLongEnough()
{
    struct Case { time_t stood; OverseerDecisions::TownVisit want; };
    Case const cases[] = {
        {0,   OverseerDecisions::TownVisit::Standing},
        {59,  OverseerDecisions::TownVisit::Standing},
        {60,  OverseerDecisions::TownVisit::Served},
        {600, OverseerDecisions::TownVisit::Served},
    };
    for (Case const& c : cases)
    {
        OverseerDecisions::TownVisit const got =
            OverseerDecisions::TownVisitStep(true, c.stood, 60);
        if (got != c.want)
            Fail("stood at the counter",
                 OverseerDecisions::TownVisitWord(c.want),
                 OverseerDecisions::TownVisitWord(got));
    }
}

// A DWELL OF ZERO IS NOT A MISSING DWELL. It is the behaviour every counter aim
// had before this trip existed: arrive, and be finished with immediately.
void ADwellOfZeroIsOverOnArrival()
{
    if (OverseerDecisions::TownVisitStep(true, 0, 0) !=
        OverseerDecisions::TownVisit::Served)
        Fail("a zero dwell", "served",
             OverseerDecisions::TownVisitWord(
                 OverseerDecisions::TownVisitStep(true, 0, 0)));
}

// The adapter and the bridge must leave enough time for one vendor pass to
// observe the leader at the counter. The default is six minutes because the
// bridge's normal vendor cadence is five minutes.
void TheDefaultDwellCoversOneVendorPass()
{
    TownTripLimits limits;
    if (limits.dwellSeconds != 6 * 60)
        Fail("default counter dwell", "360", std::to_string(limits.dwellSeconds));
}

// ------------------------------------------------ what the trip proved --

// THE ANSWER EVERY TRIP BEFORE THIS ONE WOULD HAVE GIVEN. It is a distinct word
// so that a trip which achieved nothing cannot be read as a trip that ran.
void ATripThatChangedNothingSaysSo()
{
    Proof("walked there and back", Tonight(10), Tonight(10), 1500000, 1500000,
          TownTripProof::NothingHappened);
}

// A repair is proved on the items that can wear, not on the ones reading zero
// for ever. Spending money on it is not a sale.
void ARepairIsProvedByTheGearAndNotByThePurse()
{
    Proof("nine damaged items, none afterwards, purse down",
          Need(true, 9, 2, 10), Need(true, 0, 0, 10), 1500000, 1400000,
          TownTripProof::Repaired);
}

void APartialRepairIsNotAFullOne()
{
    Proof("nine damaged, four left", Need(true, 9, 2, 10), Need(true, 4, 0, 10),
          1500000, 1400000, TownTripProof::PartlyRepaired);
}

// AND A PARTIAL REPAIR STAYS PARTIAL EVEN WHEN SOMETHING SOLD, because the
// sentence an operator needs out of the word is about the gear.
void APartialRepairIsStillPartialWhenTheBagsEmptied()
{
    Proof("half repaired and unloaded", Need(true, 9, 2, 0), Need(true, 4, 0, 12),
          1500000, 1900000, TownTripProof::PartlyRepaired);
}

// EITHER SLOTS OR MONEY IS A SALE. Auctioning frees slots and pays a deposit, so
// requiring both would report every auction as a failure.
void FreedSlotsAloneProveAnUnload()
{
    Proof("twelve slots freed, purse down by the deposit", Tonight(0), Tonight(12),
          1500000, 1480000, TownTripProof::Unloaded);
}

void APurseThatRoseAloneProvesASale()
{
    Proof("stack sold out of a slot that stays full", Tonight(4), Tonight(4),
          1500000, 1520000, TownTripProof::Unloaded);
}

// A PURSE THAT FELL IS NOT A SALE. This is the one direction that must not be
// symmetric: a repair spends money, and it must not be able to claim a sale.
void APurseThatFellIsNotASale()
{
    Proof("money spent and nothing else changed", Tonight(4), Tonight(4),
          1500000, 1400000, TownTripProof::NothingHappened);
}

void BothHalvesTogetherAreTheWholeTrip()
{
    Proof("repaired and unloaded", Need(true, 9, 2, 0), Need(true, 0, 0, 14),
          1500000, 1900000, TownTripProof::RepairedAndUnloaded);
}

// A member that was not in the world at either end proves nothing about itself.
void AMemberNobodyCouldReadProvesNothing()
{
    Proof("logged out before", Need(false, 0, 0, 0), Need(true, 0, 0, 14),
          1500000, 1900000, TownTripProof::NothingHappened);
    Proof("logged out after", Need(true, 9, 2, 0), Need(false, 0, 0, 0),
          1500000, 1900000, TownTripProof::NothingHappened);
}

void EveryProofHasAWord()
{
    TownTripProof const all[] = {
        TownTripProof::NothingHappened, TownTripProof::Unloaded,
        TownTripProof::PartlyRepaired, TownTripProof::Repaired,
        TownTripProof::RepairedAndUnloaded};
    for (TownTripProof proof : all)
        if (std::string(TownTripProofWord(proof)) == "unknown")
            Fail("a proof with no word", "a word", "unknown");
}

// --------------------------------------------------- the acceptance test --

// THE CRITERION THAT CAN NEVER PASS, AND THE ONE THAT CAN. Four of the leader's
// thirteen items read durability zero and always will. So acceptance is asked of
// the gear that has a maximum, and a character whose only zeroes are a shirt and
// three rings passes it.
void AShirtAndThreeRingsStillPass()
{
    Check("full gear, four items that cannot wear",
          TownTripMemberAccepted(Need(true, 0, 0, 20)), true);
}

void AnythingBelowItsMaximumFailsTheCriterion()
{
    Check("one item at 1 of 120", TownTripMemberAccepted(Need(true, 1, 0, 20)), false);
    Check("one item at zero of 120", TownTripMemberAccepted(Need(true, 1, 1, 20)),
          false);
}

void AMemberNobodyCouldReadHasNotPassed()
{
    Check("we could not look", TownTripMemberAccepted(Need(false, 0, 0, 0)), false);
}

}  // namespace

int main()
{
    FullGearOnRoomyBagsWantsNothing();
    ItemsThatCannotWearAreNotDamage();
    FullBagsAreAReasonToGo();
    BrokenGearOutranksFullBags();
    WornIsTheWeakestReason();
    AMemberNobodyCanReadHasNoReason();

    WornAloneNeverOpensATrip();
    EachReasonHasACounterOrNone();
    EveryReasonHasAWord();

    OneCharacterThatCannotLootTakesTheWholeFamily();
    OneBrokenMemberSetsTheDestinationForEverybody();
    AWornFamilyStaysWhereItIs();
    ATripJustTakenIsNotTakenAgain();
    ACooldownOfZeroLetsEveryTripThrough();
    NobodyGoesToTownWhileSomethingElseOwnsTheParty();
    AnEmptyRosterIsNotATrip();

    AMemberOnAnotherMapIsLeftWhereItIs();

    OnlyTheLeaderIsAimedOnTheJourney();
    EverybodyWalksTheLastFewYards();
    AFollowerAContinentAwayIsNotWalkedToTheCounter();
    AMemberInsideItsStandDownIsNotWalked();
    AFollowerAlreadyAtTheCounterTradesAnyway();
    AnUnreadableMemberIsWaitedForRatherThanFinished();
    AMemberThatWantsNothingIsFinishedWhereverItStands();
    TheCounterOutranksTheWalk();
    EveryCombinationHasAnAnswer();

    AVisitThatHasNotStartedIsNotAVisit();
    AVisitEndsWhenItHasBeenLongEnough();
    ADwellOfZeroIsOverOnArrival();
    TheDefaultDwellCoversOneVendorPass();

    ATripThatChangedNothingSaysSo();
    ARepairIsProvedByTheGearAndNotByThePurse();
    APartialRepairIsNotAFullOne();
    APartialRepairIsStillPartialWhenTheBagsEmptied();
    FreedSlotsAloneProveAnUnload();
    APurseThatRoseAloneProvesASale();
    APurseThatFellIsNotASale();
    BothHalvesTogetherAreTheWholeTrip();
    AMemberNobodyCouldReadProvesNothing();
    EveryProofHasAWord();

    AShirtAndThreeRingsStillPass();
    AnythingBelowItsMaximumFailsTheCriterion();
    AMemberNobodyCouldReadHasNotPassed();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("nothing walked them to the counter, and that was all four bugs\n");
    return EXIT_SUCCESS;
}
