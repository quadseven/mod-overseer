/*
 * The far walk's pure half (#633), decided without a world.
 *
 * WHAT IT IS FOR. The mailbox, trainer and vendor walks (#569, #621) walk a
 * guild bot off the roster to a destination within 600 or 1,000 yards, on foot
 * and in a straight line, and end the walk at the first fight. Read on the dev
 * realm, most of the site's corps and dues walks were refused as too far, and
 * one that met a mob ended without posting anything. A far walk reuses the
 * roster's travel pieces (the survey route, the ground guard, ConsiderFlight,
 * upstream's mount action); what is new, and pinned here, is:
 *
 *   - A row may ask for a cap up to FAR_WALK_MAX_YARDS, and no further.
 *   - What makes a walk far: the destination chosen lies past the goal's near
 *     cap. On the classic continents only.
 *   - The far timeout, and the far stall.
 *   - The per-bot budget and the realm ceiling, and which of their refusals move.
 *   - A fight pauses a walk and resumes it, up to an allowance.
 *   - A far walk's own flight leg is not "took a flight".
 *   - When the walk asks upstream to mount, and when it offers the flight logic
 *     a turn.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace OverseerDecisions;
namespace F = OverseerDecisions::FarWalkRefusal;
namespace M = OverseerDecisions::MailWalkRefusal;
namespace E = OverseerDecisions::ErrandWalkRefusal;

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

void CheckNumber(char const* what, uint64_t got, uint64_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %llu, wanted %llu\n", what,
                static_cast<unsigned long long>(got), static_cast<unsigned long long>(want));
    ++failures;
}

void CheckNear(char const* what, float got, float want)
{
    if (std::fabs(got - want) < 0.01f)
        return;
    std::printf("FAIL %s: got %.3f, wanted %.3f\n", what, got, want);
    ++failures;
}

void CheckText(char const* what, std::string const& got, char const* want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: '%s', wanted '%s'\n", what, got.c_str(), want);
    ++failures;
}

// A ROW MAY ASK TO GO FAR, UP TO THE FAR CAP. Measured: a dues walk refused at
// 1,953 yards, a corps walk at "beyond the cap", a pattern vendor a continent
// away. Each of these rows was malformed before #633.
void ARowMayAskForTheFarCap()
{
    MailWalkRequest mail = ParseMailWalkRequest("walk-to-mailbox max:20000");
    CheckText("a mailbox walk may ask for the far cap", mail.error, "");
    CheckNear("...and gets it", mail.maxYards, FAR_WALK_MAX_YARDS);

    mail = ParseMailWalkRequest("walk-to-mailbox max:1953");
    CheckText("the dues walk refused at 1953 yards is a row now", mail.error, "");

    mail = ParseMailWalkRequest("walk-to-mailbox max:20000.5");
    CheckText("past the far cap is malformed", mail.error, M::Malformed);

    mail = ParseMailWalkRequest("walk-to-mailbox");
    CheckNear("no cap named keeps the near cap", mail.maxYards, MAIL_WALK_MAX_YARDS);

    TrainerWalkRequest trainer = ParseTrainerWalkRequest("walk-to-trainer skill:197 max:15000");
    CheckText("a trainer walk may ask to go far", trainer.error, "");
    CheckNear("...and gets it", trainer.maxYards, 15000.f);
    trainer = ParseTrainerWalkRequest("walk-to-trainer skill:197 max:20001");
    CheckText("a trainer walk past the far cap", trainer.error, E::MalformedTrainer);
    trainer = ParseTrainerWalkRequest("walk-to-trainer skill:197");
    CheckNear("a trainer walk that names none keeps 1000", trainer.maxYards,
              ERRAND_WALK_MAX_YARDS);

    VendorWalkRequest vendor = ParseVendorWalkRequest("walk-to-vendor item:14468 max:20000");
    CheckText("the pattern vendor may be asked for from across Kalimdor", vendor.error, "");
    CheckNear("...at the far cap", vendor.maxYards, FAR_WALK_MAX_YARDS);
    vendor = ParseVendorWalkRequest("walk-to-vendor item:14468 max:99999");
    CheckText("a vendor walk past the far cap", vendor.error, E::MalformedVendor);
}

// FAR IS DECIDED ON THE DESTINATION, AGAINST THE GOAL'S OWN NEAR CAP.
void FarIsPastTheNearCap()
{
    CheckNear("a mailbox's near cap", NearWalkCapYards(WalkGoal::Mailbox), 600.f);
    CheckNear("a trainer's near cap", NearWalkCapYards(WalkGoal::Trainer), 1000.f);
    CheckNear("a vendor's near cap", NearWalkCapYards(WalkGoal::Vendor), 1000.f);

    Check("a box at 600 is near", IsFarWalk(WalkGoal::Mailbox, 600.f), false);
    Check("a box at 601 is far", IsFarWalk(WalkGoal::Mailbox, 601.f), true);
    Check("a vendor at 900 is near", IsFarWalk(WalkGoal::Vendor, 900.f), false);
    Check("a vendor at 1001 is far", IsFarWalk(WalkGoal::Vendor, 1001.f), true);
    Check("a trainer at 1000 is near", IsFarWalk(WalkGoal::Trainer, 1000.f), false);

    Check("Eastern Kingdoms", FarWalkMapAllowed(0), true);
    Check("Kalimdor", FarWalkMapAllowed(1), true);
    Check("Outland is out of scope on this realm", FarWalkMapAllowed(530), false);
    Check("Northrend is out of scope on this realm", FarWalkMapAllowed(571), false);
    Check("a dungeon is no continent", FarWalkMapAllowed(36), false);
}

void TheFarClock()
{
    CheckNumber("a far walk's floor", FarWalkTimeoutSeconds(0.f), MAIL_WALK_TIMEOUT_FLOOR_SECONDS);
    // 60 + 1953 / 3.5 = 618: past the near ceiling of 300, under the far one.
    CheckNumber("the 1953 yard walk", FarWalkTimeoutSeconds(1953.f), 618);
    Check("a far walk outlasts the near ceiling",
          FarWalkTimeoutSeconds(1953.f) > MAIL_WALK_TIMEOUT_CEILING_SECONDS, true);
    CheckNumber("a continent is capped", FarWalkTimeoutSeconds(FAR_WALK_MAX_YARDS),
                FAR_WALK_TIMEOUT_CEILING_SECONDS);
    CheckNumber("the near clock is untouched", MailWalkTimeoutSeconds(1953.f),
                MAIL_WALK_TIMEOUT_CEILING_SECONDS);

    MailWalkFacts f;
    f.present = true;
    f.alive = true;
    f.sameMap = true;
    f.timeoutMs = 600000;
    f.sinceProgressMs = MAIL_WALK_STALL_SECONDS * 1000u;
    CheckText("a near walk stalls at its own clock", MailWalkStateWord(JudgeMailWalk(f)),
              "stalled");
    f.stallMs = FAR_WALK_STALL_SECONDS * 1000u;
    CheckText("a far walk round a hill is still walking", MailWalkStateWord(JudgeMailWalk(f)),
              "walking");
    f.sinceProgressMs = FAR_WALK_STALL_SECONDS * 1000u;
    CheckText("...until its own stall", MailWalkStateWord(JudgeMailWalk(f)), "stalled");
}

void TheBudget()
{
    std::vector<int64_t> none;
    CheckText("a first far walk may start", FarWalkBudgetGate(none, 10000, 0), "");

    std::vector<int64_t> one = {9000};
    CheckText("a second in the hour may start", FarWalkBudgetGate(one, 10000, 0), "");

    std::vector<int64_t> two = {7000, 9000};
    CheckText("a third in the hour is refused", FarWalkBudgetGate(two, 10000, 0),
              F::BotBudgetSpent);
    CheckText("...and allowed once the first leaves the window",
              FarWalkBudgetGate(two, 7000 + FAR_WALK_BUDGET_WINDOW_SECONDS, 0), "");

    CheckText("the realm ceiling", FarWalkBudgetGate(none, 10000, FAR_WALKS_AT_ONCE),
              F::RealmFull);
    CheckText("the realm ceiling is named before the bot's", FarWalkBudgetGate(two, 10000, 9),
              F::RealmFull);

    std::vector<int64_t> starts = {1000, 5000, 9000};
    PruneFarWalkStarts(starts, 1000 + FAR_WALK_BUDGET_WINDOW_SECONDS);
    CheckNumber("the prune keeps one hour", starts.size(), 2);

    Check("the bot budget moves with the clock", FarWalkRefusalRetryable(F::BotBudgetSpent), true);
    Check("the realm ceiling moves", FarWalkRefusalRetryable(F::RealmFull), true);
    Check("the map does not", FarWalkRefusalRetryable(F::NotOnAContinent), false);
    Check("a mailbox wall is not a far wall", FarWalkRefusalRetryable(M::MailboxTooFar), false);
}

MailWalkFacts OnTheWay()
{
    MailWalkFacts f;
    f.present = true;
    f.alive = true;
    f.sameMap = true;
    f.waitedMs = 10000;
    f.timeoutMs = 600000;
    return f;
}

// A FIGHT PAUSES THE WALK. Measured: "Zora ... entered combat on the way to the
// mailbox" ended a dues walk 341 yards from the box, and no letter was posted.
void AFightPausesTheWalk()
{
    MailWalkFacts f = OnTheWay();
    f.inCombat = true;
    CheckText("a fight pauses the walk", MailWalkStateWord(JudgeMailWalk(f)), "paused");
    CheckText("a pause is no ending", MailWalkEndReason(JudgeMailWalk(f)), "");
    CheckText("a trainer walk pauses too", WalkEndReasonFor(WalkGoal::Trainer, JudgeMailWalk(f)),
              "");

    f.combatMs = MAIL_WALK_COMBAT_PAUSE_SECONDS * 1000u - 1u;
    CheckText("still inside the allowance", MailWalkStateWord(JudgeMailWalk(f)), "paused");
    f.combatMs = MAIL_WALK_COMBAT_PAUSE_SECONDS * 1000u;
    CheckText("past the allowance the walk ends", MailWalkEndReason(JudgeMailWalk(f)),
              M::EnteredCombat);
    CheckText("...with the vendor's own sentence",
              WalkEndReasonFor(WalkGoal::Vendor, JudgeMailWalk(f)), E::VendorCombat);

    // Death outranks the pause: a walker killed mid-fight is over.
    f = OnTheWay();
    f.inCombat = true;
    f.alive = false;
    CheckText("death outranks the pause", MailWalkEndReason(JudgeMailWalk(f)), M::Died);

    // And the fight over, the walk is walking again.
    f = OnTheWay();
    f.combatMs = 60000;
    CheckText("after the fight it walks on", MailWalkStateWord(JudgeMailWalk(f)), "walking");
}

// THE WALK'S OWN FLIGHT IS THE WALK GOING WELL.
void TheWalksOwnFlight()
{
    MailWalkFacts f = OnTheWay();
    f.inFlight = true;
    CheckText("a taxi the walk did not board ends it", MailWalkEndReason(JudgeMailWalk(f)),
              M::TookFlight);

    f.onFlightLeg = true;
    CheckText("the walk's own taxi is the walk", MailWalkStateWord(JudgeMailWalk(f)), "flying");
    CheckText("...and no ending", MailWalkEndReason(JudgeMailWalk(f)), "");

    // Walking to the flight master is the leg too: no stall, no arrival.
    f = OnTheWay();
    f.onFlightLeg = true;
    f.sinceProgressMs = 10u * 60u * 1000u;
    f.mailboxInReach = true;
    CheckText("the walk to the flight master is no stall", MailWalkStateWord(JudgeMailWalk(f)),
              "flying");

    f.waitedMs = f.timeoutMs;
    CheckText("the clock still binds a flight leg", MailWalkEndReason(JudgeMailWalk(f)),
              M::TimedOut);

    f = OnTheWay();
    f.onFlightLeg = true;
    f.inCombat = true;
    CheckText("a fight on the way to the flight master pauses",
              MailWalkStateWord(JudgeMailWalk(f)), "paused");
}

void WhenToMount()
{
    FarWalkMountFacts m;
    m.far = true;
    m.outdoors = true;
    m.yardsToGo = 1500.f;
    Check("a far walker on foot mounts", FarWalkShouldMount(m), true);

    FarWalkMountFacts near = m;
    near.far = false;
    Check("a near walk walks as it always did", FarWalkShouldMount(near), false);

    FarWalkMountFacts x = m;
    x.mounted = true;
    Check("already mounted", FarWalkShouldMount(x), false);
    x = m;
    x.casting = true;
    Check("not over a cast", FarWalkShouldMount(x), false);
    x = m;
    x.outdoors = false;
    Check("not indoors", FarWalkShouldMount(x), false);
    x = m;
    x.inCombat = true;
    Check("not in a fight", FarWalkShouldMount(x), false);
    x = m;
    x.yardsToGo = FAR_WALK_MOUNT_YARDS;
    Check("not for the last yards", FarWalkShouldMount(x), false);
    x = m;
    x.tries = 1;
    x.sinceLastTrySeconds = FAR_WALK_MOUNT_RETRY_SECONDS - 1;
    Check("a retry waits its clock", FarWalkShouldMount(x), false);
    x.sinceLastTrySeconds = FAR_WALK_MOUNT_RETRY_SECONDS;
    Check("...and then tries", FarWalkShouldMount(x), true);
    x.tries = FAR_WALK_MOUNT_TRIES;
    Check("the tries run out", FarWalkShouldMount(x), false);
}

void WhenToOfferAFlight()
{
    FarWalkFlightFacts f;
    f.far = true;
    f.carriesNewRpg = true;
    f.yardsToGo = 4000.f;
    Check("a far walker a continent away may fly", FarWalkMayAskFlight(f), true);

    FarWalkFlightFacts x = f;
    x.far = false;
    Check("a near walk never flies", FarWalkMayAskFlight(x), false);
    x = f;
    x.askedThisStretch = true;
    Check("once per stretch on the ground", FarWalkMayAskFlight(x), false);
    x = f;
    x.carriesNewRpg = false;
    Check("upstream's flight action needs new rpg", FarWalkMayAskFlight(x), false);
    x = f;
    x.grouped = true;
    Check("a grouped bot is left to its group", FarWalkMayAskFlight(x), false);
    x = f;
    x.yardsToGo = FAR_WALK_FLIGHT_MIN_YARDS - 1.f;
    Check("a short way is walked", FarWalkMayAskFlight(x), false);
    x = f;
    x.flights = FAR_WALK_FLIGHTS_MAX;
    Check("the flights run out", FarWalkMayAskFlight(x), false);
}

void TheNewWordsAreSaid()
{
    CheckText("paused", MailWalkStateWord(MailWalkState::Paused), "paused");
    CheckText("flying", MailWalkStateWord(MailWalkState::Flying), "flying");
    CheckText("a pause names no mailbox ending", MailWalkEndReason(MailWalkState::Paused), "");
    CheckText("a flight names no mailbox ending", MailWalkEndReason(MailWalkState::Flying), "");
}

}  // namespace

int main()
{
    ARowMayAskForTheFarCap();
    FarIsPastTheNearCap();
    TheFarClock();
    TheBudget();
    AFightPausesTheWalk();
    TheWalksOwnFlight();
    WhenToMount();
    WhenToOfferAFlight();
    TheNewWordsAreSaid();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the far walk decisions hold\n");
    return EXIT_SUCCESS;
}
