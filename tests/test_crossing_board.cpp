/*
 * Getting a family ONTO a boat and OFF it again (#279), which the crossing
 * could not do: every crossing between Kalimdor and the Eastern Kingdoms was
 * refused "at 'walk to the berth': no boardable place on map 1 has been
 * established", logged on the dev realm for scarlet-cathedral and
 * scarlet-library runs between 2026-09-12 and 2026-09-14.
 *
 * THE FOUR MISSING FACTS, AND WHERE EACH NOW COMES FROM:
 *
 *   * A BERTH. Not derived from the mooring: read out of the travel survey,
 *     whose walks into each transport's own node end on the pier, and checked
 *     against the deck height the transport's own crew stands at (PickBerth).
 *   * DOCKED OR NOT. The core's own stop test on the transport's clock
 *     (ReadDock).
 *   * WHOSE BOAT. Its crew's reaction to the leader (ReadCrewWelcome). The dev
 *     realm offered an Alliance leader the Horde zeppelin to Undercity.
 *   * WHICH BOAT. Priced in yards rather than "whichever is on this map this
 *     poll" (PriceCrossings).
 *
 * The berth cases replay the shipped survey and the dev realm's world data,
 * read 2026-09-24, so the numbers below are real points and not fixtures made
 * up to pass.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::BerthCandidate;
using OverseerDecisions::BerthLimits;
using OverseerDecisions::CrewWelcome;
using OverseerDecisions::CrossingAction;
using OverseerDecisions::CrossingActionName;
using OverseerDecisions::CrossingExplanation;
using OverseerDecisions::CrossingLeg;
using OverseerDecisions::CrossingLegName;
using OverseerDecisions::CrossingLimits;
using OverseerDecisions::CrossingMember;
using OverseerDecisions::CrossingOffer;
using OverseerDecisions::CrossingPrice;
using OverseerDecisions::CrossingPriceLimits;
using OverseerDecisions::CrossingStep;
using OverseerDecisions::CrossingWorld;
using OverseerDecisions::DockReading;
using OverseerDecisions::OfferVerdict;
using OverseerDecisions::OfferVerdictName;
using OverseerDecisions::PickBerth;
using OverseerDecisions::PriceCrossings;
using OverseerDecisions::ReadCrewWelcome;
using OverseerDecisions::ReadCrossing;
using OverseerDecisions::ReadDock;
using OverseerDecisions::TransportStop;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckAction(char const* what, CrossingAction got, CrossingAction want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, CrossingActionName(got),
                CrossingActionName(want));
    ++failures;
}

void CheckLeg(char const* what, CrossingLeg got, CrossingLeg want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, CrossingLegName(got),
                CrossingLegName(want));
    ++failures;
}

void CheckInt(char const* what, long got, long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %ld, wanted %ld\n", what, got, want);
    ++failures;
}

void CheckVerdict(char const* what, OfferVerdict got, OfferVerdict want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, OfferVerdictName(got),
                OfferVerdictName(want));
    ++failures;
}

void CheckSays(char const* what, std::string const& said, char const* fragment)
{
    if (said.find(fragment) != std::string::npos)
        return;
    std::printf("FAIL %s: '%s' does not mention '%s'\n", what, said.c_str(), fragment);
    ++failures;
}

// Kalimdor to the Eastern Kingdoms, every fact in hand, the transport NOT yet
// docked. Each case turns on the one fact it is about.
CrossingWorld Docks()
{
    CrossingWorld w;
    w.originMap = 1;
    w.destinationMap = 0;
    w.transportFound = true;
    w.berthKnown = true;
    w.landingKnown = true;
    w.mooringKnown = true;
    return w;
}

CrossingLimits Limits()
{
    CrossingLimits l;
    l.berthArrivedYards = 12.f;
    l.gatherYards = 30.f;
    l.minBoardDwellMs = 15000;
    return l;
}

CrossingMember Leader(std::uint32_t map, float berthDistance, bool aboard = false)
{
    CrossingMember m;
    m.readable = true;
    m.isLeader = true;
    m.aboard = aboard;
    m.mapId = map;
    m.berthDistance = berthDistance;
    return m;
}

CrossingMember Follower(std::uint32_t map, float fromLeader, bool aboard = false)
{
    CrossingMember m;
    m.readable = true;
    m.isLeader = false;
    m.aboard = aboard;
    m.mapId = map;
    m.leaderDistance = fromLeader;
    return m;
}

// ------------------------------------------------------------ boarding --

// THE CASE THE WHOLE CHANGE IS FOR: at the berth, the boat in, a minute of its
// stop left, the family around the leader. He steps aboard.
void ADockedBoatWithTheFamilyAtTheBerthIsBoarded()
{
    CrossingWorld w = Docks();
    w.dockedAtOrigin = true;
    w.dwellLeftMs = 55000;
    std::vector<CrossingMember> members = {Leader(1, 3.f), Follower(1, 4.f),
                                           Follower(1, 6.f), Follower(1, 9.f),
                                           Follower(1, 2.f)};
    CrossingStep const step = ReadCrossing(w, members, Limits());
    CheckAction("docked, gathered, time left: board", step.action, CrossingAction::Board);
    CheckLeg("from the wait-for-transport leg", step.leg, CrossingLeg::WaitForTransport);
    CheckInt("four followers gathered", static_cast<long>(step.gathered), 4);
    CheckSays("and says the map confirms the deck", CrossingExplanation(step, w),
              "the map confirmed");
    CheckSays("and says how much of the stop is left", CrossingExplanation(step, w), "55s");
}

// Nothing steps onto a deck that is not there. Before this change the only
// answer at the berth was Hold, and it still is while the boat is away.
void AtTheBerthWithTheBoatAwayIsStillAHold()
{
    std::vector<CrossingMember> members = {Leader(1, 3.f), Follower(1, 4.f)};
    CrossingStep const step = ReadCrossing(Docks(), members, Limits());
    CheckAction("boat away: hold", step.action, CrossingAction::Hold);
    CheckSays("and says it waits for the dock", CrossingExplanation(step, Docks()),
              "the transport to dock");
}

// A step started as the boat casts off is a character walked into the harbour.
void TooLittleOfTheStopLeftWaitsForTheNextOne()
{
    CrossingWorld w = Docks();
    w.dockedAtOrigin = true;
    w.dwellLeftMs = 14999;
    std::vector<CrossingMember> members = {Leader(1, 3.f), Follower(1, 4.f)};
    CrossingStep const step = ReadCrossing(w, members, Limits());
    CheckAction("under the minimum dwell: hold", step.action, CrossingAction::Hold);
    CheckSays("and says it waits for the next stop", CrossingExplanation(step, w),
              "the next stop");

    w.dwellLeftMs = 15000;
    CheckAction("at exactly the minimum: board", ReadCrossing(w, members, Limits()).action,
                CrossingAction::Board);
}

// THE PARTY BOARDS TOGETHER. A follower out of reach of the leader would be
// left on the pier by upstream's boarding assist, and the family would land
// four strong on the wrong continent from the fifth.
void AStragglerOnThePierHoldsTheBoarding()
{
    CrossingWorld w = Docks();
    w.dockedAtOrigin = true;
    w.dwellLeftMs = 55000;
    std::vector<CrossingMember> members = {Leader(1, 3.f), Follower(1, 4.f),
                                           Follower(1, 31.f)};
    CrossingStep const step = ReadCrossing(w, members, Limits());
    CheckAction("one follower 31 yards out: hold", step.action, CrossingAction::Hold);
    CheckInt("one gathered", static_cast<long>(step.gathered), 1);
    CheckSays("and names how many are still out of reach", CrossingExplanation(step, w),
              "1 member(s) still too far");
}

// But a follower already aboard or already across is not a straggler.
void MembersAlreadyAboardOrAcrossDoNotHoldIt()
{
    CrossingWorld w = Docks();
    w.dockedAtOrigin = true;
    w.dwellLeftMs = 40000;
    std::vector<CrossingMember> members = {Leader(1, 3.f), Follower(1, 200.f, true),
                                           Follower(0, 0.f)};
    CheckAction("aboard and across do not hold it",
                ReadCrossing(w, members, Limits()).action, CrossingAction::Board);
}

// A limit nobody set lets nobody but a lone leader board. Fail closed.
void AnUnsetGatherLimitBoardsOnlyALoneLeader()
{
    CrossingWorld w = Docks();
    w.dockedAtOrigin = true;
    w.dwellLeftMs = 40000;
    CrossingLimits l = Limits();
    l.gatherYards = 0.f;
    CheckAction("a follower two yards out, no limit: hold",
                ReadCrossing(w, {Leader(1, 3.f), Follower(1, 2.f)}, l).action,
                CrossingAction::Hold);
    CheckAction("a lone leader: board", ReadCrossing(w, {Leader(1, 3.f)}, l).action,
                CrossingAction::Board);
}

// Docked is not a licence to board from anywhere: the leader walks to the
// berth first, and the step aboard is only ever a few yards.
void ADockedBoatDoesNotShortCutTheWalk()
{
    CrossingWorld w = Docks();
    w.dockedAtOrigin = true;
    w.dwellLeftMs = 55000;
    CheckAction("docked but 400 yards out: walk",
                ReadCrossing(w, {Leader(1, 400.f), Follower(1, 3.f)}, Limits()).action,
                CrossingAction::Walk);
}

// ------------------------------------------------------------- walking off --

void TheLeaderIsWalkedOffAtADockedFarEnd()
{
    CrossingWorld w = Docks();
    w.dockedAtDestination = true;
    w.dwellLeftMs = 50000;
    std::vector<CrossingMember> members = {Leader(0, 0.f, true), Follower(0, 0.f, true),
                                           Follower(0, 0.f, true)};
    CrossingStep const step = ReadCrossing(w, members, Limits());
    CheckAction("aboard at the docked far end: walk off", step.action,
                CrossingAction::WalkOff);
    CheckLeg("on the disembark leg", step.leg, CrossingLeg::Disembark);
    CheckSays("and says the followers follow", CrossingExplanation(step, w),
              "followers follow him off");
}

// Still coming in: nobody steps off yet, and that is the old Disembark.
void NotYetDockedIsNotAWalkOff()
{
    std::vector<CrossingMember> members = {Leader(0, 0.f, true), Follower(0, 0.f, true)};
    CrossingStep const step = ReadCrossing(Docks(), members, Limits());
    CheckAction("aboard, not docked: disembark (wait)", step.action,
                CrossingAction::Disembark);
    CheckSays("and says it has not docked", CrossingExplanation(step, Docks()),
              "has not docked");
}

// A step off started as the boat casts off is carried out over the water.
void AStopAboutToEndIsNotAWalkOff()
{
    CrossingWorld w = Docks();
    w.dockedAtDestination = true;
    w.dwellLeftMs = 4000;
    CrossingStep const step = ReadCrossing(w, {Leader(0, 0.f, true)}, Limits());
    CheckAction("docked, four seconds left: disembark (wait)", step.action,
                CrossingAction::Disembark);
    CheckSays("and says too little is left", CrossingExplanation(step, w),
              "too little of the stop");
}

// A landing nothing established is not somewhere to step, docked or not.
void NoLandingIsNoWalkOff()
{
    CrossingWorld w = Docks();
    w.dockedAtDestination = true;
    w.landingKnown = false;
    CheckAction("docked, no landing: disembark (wait)",
                ReadCrossing(w, {Leader(0, 0.f, true)}, Limits()).action,
                CrossingAction::Disembark);
}

// The leader is off, a follower is still on: the follower follows him off, and
// nothing is ordered to anybody.
void TheLeaderAshoreWaitsForHisFollowers()
{
    CrossingWorld w = Docks();
    w.dockedAtDestination = true;
    std::vector<CrossingMember> members = {Leader(0, 0.f), Follower(0, 0.f, true)};
    CrossingStep const step = ReadCrossing(w, members, Limits());
    CheckAction("leader off, follower on: disembark", step.action,
                CrossingAction::Disembark);
    CheckSays("and says they follow him", CrossingExplanation(step, w), "follow him off");
}

// Docked at the ORIGIN is not docked at the destination: a leader aboard on the
// origin map rides, whatever the dock says.
void AboardAtTheOriginRidesEvenWhileDocked()
{
    CrossingWorld w = Docks();
    w.dockedAtOrigin = true;
    w.dwellLeftMs = 30000;
    CheckAction("aboard at the origin: ride",
                ReadCrossing(w, {Leader(1, 0.f, true), Follower(1, 2.f)}, Limits()).action,
                CrossingAction::Ride);
}

// ----------------------------------------------------------- the dock read --

// The Lady Mehley's two stops, as ArriveTime/DepartureTime pairs in the shape
// the core builds them: a sixty second stop at each end of its path.
std::vector<TransportStop> TwoStops()
{
    return {{0, 10000, 70000}, {1, 200000, 260000}};
}

void DockedIsTheCoresOwnInterval()
{
    DockReading const before = ReadDock(9999, 400000, TwoStops());
    Check("a millisecond early is not docked", before.docked, false);

    DockReading const at = ReadDock(10000, 400000, TwoStops());
    Check("arrival is docked", at.docked, true);
    CheckInt("at the first stop", static_cast<long>(at.stop), 0);
    CheckInt("with the whole stop left", static_cast<long>(at.dwellLeftMs), 60000);

    DockReading const leaving = ReadDock(70000, 400000, TwoStops());
    Check("departure is not docked", leaving.docked, false);

    // The clock is the progress MODULO the period, so the fourth circuit reads
    // like the first.
    DockReading const later = ReadDock(3 * 400000 + 230000, 400000, TwoStops());
    Check("a later circuit is docked", later.docked, true);
    CheckInt("at the second stop", static_cast<long>(later.stop), 1);
    CheckInt("with thirty seconds left", static_cast<long>(later.dwellLeftMs), 30000);
}

// The ride from leaving one stop to reaching the other, including a path whose
// arrival comes round again at the start of the next period.
void TheRideIsMeasuredRoundThePeriod()
{
    CheckInt("stop one to stop two", static_cast<long>(OverseerDecisions::TransportRideMs(
                                         70000, 200000, 400000)),
             130000);
    CheckInt("stop two round to stop one",
             static_cast<long>(OverseerDecisions::TransportRideMs(260000, 10000, 400000)),
             150000);
    CheckInt("no period is no ride",
             static_cast<long>(OverseerDecisions::TransportRideMs(260000, 10000, 0)), 0);
}

void NoClockIsNeverDocked()
{
    Check("a zero period is not docked", ReadDock(10000, 0, TwoStops()).docked, false);
    Check("no stops is not docked", ReadDock(10000, 400000, {}).docked, false);
    // A waypoint the transport passes through has an empty interval.
    Check("an empty interval is not a stop",
          ReadDock(5000, 400000, {{0, 5000, 5000}}).docked, false);
}

// -------------------------------------------------------------- the berth --

BerthLimits Berth()
{
    BerthLimits l;
    l.reachYards = 40.f;
    l.deckStepYards = 3.f;
    return l;
}

// Ratchet, read off the shipped survey: the last points of three walks into
// the Maiden's Fancy's own node. The deck levels are the mooring's z (0) plus
// the heights its crew stands at (6.1, 11.6, 12.0, 14.1, 17.9, 18.3).
void TheRatchetPierIsFoundAndTheWaterIsNot()
{
    std::vector<BerthCandidate> const ratchet = {
        {-1005.02f, -3845.60f, 0.614098f},  // the swim in from Durotar
        {-996.542f, -3818.65f, 6.04727f},   // the pier, further back
        {-1002.67f, -3826.93f, 5.44743f},   // the pier's end
    };
    std::vector<float> const decks = {6.1f, 11.6f, 12.0f, 14.1f, 17.9f, 18.3f};
    CheckInt("the pier's end, not the water", PickBerth(ratchet, -1005.61f, -3841.65f, decks,
                                                        Berth()),
             2);
}

// Orgrimmar's tower: the zeppelin's origin is 18 yards above its deck, and the
// platform under it is level with the deck (crew at -17.7 below the origin).
void TheOrgrimmarPlatformIsLevelWithTheZeppelinDeck()
{
    std::vector<BerthCandidate> const tower = {{1319.07f, -4658.09f, 53.6952f}};
    std::vector<float> const decks = {71.8604f - 23.7f, 71.8604f - 17.7f, 71.8604f - 14.4f};
    CheckInt("the platform is a berth", PickBerth(tower, 1318.11f, -4658.05f, decks, Berth()),
             0);
}

// Grom'gol: every surveyed walk into the Iron Eagle's node ends on the ground
// below the tower, 49 yards under the mooring. None of it is a berth, and a
// crossing that would land there is refused rather than walked.
void TheFootOfAZeppelinTowerIsNotABerth()
{
    std::vector<BerthCandidate> const ground = {
        {-12462.5f, 227.881f, 0.578255f},
        {-12463.1f, 227.673f, 0.572068f},
        {-12467.9f, 232.327f, 0.534752f},
    };
    std::vector<float> const decks = {49.5344f - 23.7f, 49.5344f - 17.7f, 49.5344f - 15.2f};
    CheckInt("nothing at the foot of the tower", PickBerth(ground, -12464.0f, 231.565f, decks,
                                                           Berth()),
             -1);
}

void NoCrewNoDeckNoBerth()
{
    std::vector<BerthCandidate> const pier = {{-1002.67f, -3826.93f, 5.44743f}};
    CheckInt("no deck levels at all", PickBerth(pier, -1005.61f, -3841.65f, {}, Berth()), -1);
    BerthLimits bad = Berth();
    bad.reachYards = -40.f;
    CheckInt("a negative reach admits nothing",
             PickBerth(pier, -1005.61f, -3841.65f, {6.1f}, bad), -1);
    CheckInt("too far from the mooring", PickBerth({{-1002.67f, -3700.f, 6.f}}, -1005.61f,
                                                   -3841.65f, {6.1f}, Berth()),
             -1);
}

// ---------------------------------------------------------------- the crew --

void WhoseBoatItIs()
{
    Check("hostile crew", ReadCrewWelcome(21, 12) == CrewWelcome::Unwelcome, true);
    Check("one hostile is enough", ReadCrewWelcome(8, 1) == CrewWelcome::Unwelcome, true);
    Check("a neutral crew serves", ReadCrewWelcome(8, 0) == CrewWelcome::Welcome, true);
    Check("no crew read is unknown", ReadCrewWelcome(0, 0) == CrewWelcome::Unknown, true);
}

// -------------------------------------------------------------- the price --

CrossingOffer Offer(std::uint32_t entry, float toBerth, float fromLanding)
{
    CrossingOffer o;
    o.entry = entry;
    o.crew = CrewWelcome::Welcome;
    o.berthKnown = true;
    o.landingKnown = true;
    o.toBerthYards = toBerth;
    o.landingToGoalYards = fromLanding;
    o.rideSeconds = 150.f;
    o.periodSeconds = 360.f;
    return o;
}

CrossingPriceLimits Rate()
{
    CrossingPriceLimits l;
    l.yardsPerSecond = 7.f;
    return l;
}

// THE DEFECT MEASURED ON THE DEV REALM: an Alliance leader offered the Horde
// zeppelin. It is refused however cheap it is.
void TheOtherSidesZeppelinIsNeverPicked()
{
    CrossingOffer zeppelin = Offer(164871, 50.f, 50.f);
    zeppelin.crew = CrewWelcome::Unwelcome;
    CrossingOffer const boat = Offer(176231, 3000.f, 2500.f);
    CrossingPrice const price = PriceCrossings({zeppelin, boat}, Rate());
    CheckVerdict("the zeppelin is the wrong side's", price.verdicts[0],
                 OfferVerdict::WrongSide);
    CheckInt("the boat is picked", price.pick, 1);
}

void TheCheaperCrossingWins()
{
    // Theramore to Menethil against Ratchet to Booty Bay, for a goal in the
    // north of the Eastern Kingdoms: the second is nearer to board and far
    // further from the goal once landed.
    CrossingPrice const price =
        PriceCrossings({Offer(176231, 2600.f, 1900.f), Offer(20808, 1800.f, 6000.f)}, Rate());
    CheckInt("the cheaper total wins", price.pick, 0);
    CheckInt("priced as walk plus water",
             static_cast<long>(price.yards[0]), static_cast<long>(2600 + 1900 + (150 + 180) * 7));
}

// A crossing already under way keeps its boat: the walk to its berth shrinks
// as the leader walks, and re-choosing every poll could turn him round.
void TheIncumbentIsKeptWhileItPrices()
{
    CrossingPriceLimits l = Rate();
    l.incumbentEntry = 20808;
    CrossingPrice const price =
        PriceCrossings({Offer(176231, 100.f, 100.f), Offer(20808, 1800.f, 6000.f)}, l);
    CheckInt("the incumbent is kept", price.pick, 1);
    Check("and says so", price.keptIncumbent, true);

    CrossingOffer gone = Offer(20808, 1800.f, 6000.f);
    gone.landingKnown = false;
    CrossingPrice const repriced = PriceCrossings({Offer(176231, 100.f, 100.f), gone}, l);
    CheckInt("an incumbent that no longer prices is dropped", repriced.pick, 0);
    CheckVerdict("and says why", repriced.verdicts[1], OfferVerdict::NoLanding);
}

void NothingPricesNothingIsPicked()
{
    CrossingOffer unread = Offer(1, 10.f, 10.f);
    unread.crew = CrewWelcome::Unknown;
    CrossingOffer berthless = Offer(2, 10.f, 10.f);
    berthless.berthKnown = false;
    CrossingOffer nonsense = Offer(3, -10.f, 10.f);
    CrossingPrice const price = PriceCrossings({unread, berthless, nonsense}, Rate());
    CheckInt("nothing picked", price.pick, -1);
    CheckVerdict("crew unread", price.verdicts[0], OfferVerdict::CrewUnread);
    CheckVerdict("no berth", price.verdicts[1], OfferVerdict::NoBerth);
    CheckVerdict("bad figures", price.verdicts[2], OfferVerdict::BadFigures);
    CheckInt("and an empty list picks nothing", PriceCrossings({}, Rate()).pick, -1);
}

// ------------------------------------------------------ telling the bridge --

// The bridge refuses a door on the other continent while nothing can cross to
// it, and the build report is how it learns that this build can.
void TheBuildSaysItBoards()
{
    std::vector<OverseerDecisions::BuildFact> const facts =
        OverseerDecisions::BuildReport("AzerothCore rev. 47960183bb03+", {});
    bool said = false;
    for (OverseerDecisions::BuildFact const& fact : facts)
        if (fact.name == "crossing")
            said = fact.value == "boards" &&
                   fact.source == std::string(OverseerDecisions::SOURCE_COMPILED);
    Check("the build report says crossing = boards, compiled", said, true);
}

} // namespace

int main()
{
    ADockedBoatWithTheFamilyAtTheBerthIsBoarded();
    AtTheBerthWithTheBoatAwayIsStillAHold();
    TooLittleOfTheStopLeftWaitsForTheNextOne();
    AStragglerOnThePierHoldsTheBoarding();
    MembersAlreadyAboardOrAcrossDoNotHoldIt();
    AnUnsetGatherLimitBoardsOnlyALoneLeader();
    ADockedBoatDoesNotShortCutTheWalk();

    TheLeaderIsWalkedOffAtADockedFarEnd();
    NotYetDockedIsNotAWalkOff();
    AStopAboutToEndIsNotAWalkOff();
    NoLandingIsNoWalkOff();
    TheLeaderAshoreWaitsForHisFollowers();
    AboardAtTheOriginRidesEvenWhileDocked();

    DockedIsTheCoresOwnInterval();
    NoClockIsNeverDocked();
    TheRideIsMeasuredRoundThePeriod();

    TheRatchetPierIsFoundAndTheWaterIsNot();
    TheOrgrimmarPlatformIsLevelWithTheZeppelinDeck();
    TheFootOfAZeppelinTowerIsNotABerth();
    NoCrewNoDeckNoBerth();

    WhoseBoatItIs();

    TheOtherSidesZeppelinIsNeverPicked();
    TheCheaperCrossingWins();
    TheIncumbentIsKeptWhileItPrices();
    NothingPricesNothingIsPicked();

    TheBuildSaysItBoards();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("a family boards a docked transport together, rides it, and is walked off "
                "at the far end; only its own side's boats, the cheapest first\n");
    return 0;
}
