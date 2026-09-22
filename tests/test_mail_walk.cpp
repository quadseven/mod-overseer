/*
 * The mailbox walk's pure half (#569), decided without a world.
 *
 * WHAT IT IS FOR. A `kind='mail'` row reading `walk-to-mailbox` walks one
 * online bot that is not on the roster to the nearest mailbox on its map, so a
 * later `send` row finds a box in reach. The executor in mod_overseer.cpp reads
 * the world; everything it decides with what it read lives in
 * overseer_decisions and is pinned here:
 *
 *   - The grammar: the verb alone, or with one `max:<yards>` no larger than
 *     the 600 yard cap. Anything else is refused rather than guessed at.
 *   - The gate, in order: no bot AI, out of the world, logging out, dead, on a
 *     taxi, fighting, in an instance, on the roster, a follower, already
 *     walking, held by another verb.
 *   - Which mailbox: the nearest one not on the other side's ground, within
 *     the cap, with the three different refusals kept apart.
 *   - How far one leg aims, which is what keeps every leg under the 296 yards
 *     PathGenerator will smooth.
 *   - The timeout, and the per-poll verdict: what ends a walk, and in which
 *     order, so combat is never out-ranked by an arrival.
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
namespace R = OverseerDecisions::MailWalkRefusal;

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

void TheRowIsRecognisedByItsFirstWord()
{
    Check("the verb alone is a walk", IsMailWalkRow("walk-to-mailbox"), true);
    Check("with a cap is a walk", IsMailWalkRow("walk-to-mailbox max:300"), true);
    Check("leading spaces are a walk", IsMailWalkRow("  walk-to-mailbox"), true);
    Check("a malformed walk still routes to the walk", IsMailWalkRow("walk-to-mailbox now"),
          true);
    Check("send is not a walk", IsMailWalkRow("send item:12 subject: hi"), false);
    Check("take-item is not a walk", IsMailWalkRow("take-item mail:1 item:2"), false);
    Check("empty is not a walk", IsMailWalkRow(""), false);
    Check("a prefix is not a walk", IsMailWalkRow("walk-to-mailboxes"), false);
}

void TheGrammarTakesOneOptionalCap()
{
    MailWalkRequest plain = ParseMailWalkRequest("walk-to-mailbox");
    CheckText("the verb alone parses", plain.error, "");
    CheckNear("the default cap is the site's own", plain.maxYards, 600.f);

    MailWalkRequest capped = ParseMailWalkRequest("walk-to-mailbox max:250.5");
    CheckText("a cap parses", capped.error, "");
    CheckNear("the cap is read", capped.maxYards, 250.5f);

    MailWalkRequest atCap = ParseMailWalkRequest("walk-to-mailbox max:600");
    CheckText("the cap itself is allowed", atCap.error, "");

    char const* const bad[] = {
        "walk-to-mailbox max:601",       // a row cannot raise the cap
        "walk-to-mailbox max:0",
        "walk-to-mailbox max:-5",
        "walk-to-mailbox max:",
        "walk-to-mailbox max:1e3",
        "walk-to-mailbox max:12yards",
        "walk-to-mailbox max:1.2.3",
        "walk-to-mailbox max:.",
        "walk-to-mailbox cap:300",
        "walk-to-mailbox max:300 max:200",
        "walk-to-mailbox at:0:1,2,3",
        "send",
        "",
    };
    for (char const* row : bad)
    {
        MailWalkRequest r = ParseMailWalkRequest(row);
        if (std::string(r.error) != R::Malformed)
        {
            std::printf("FAIL '%s' should be malformed, got '%s'\n", row, r.error);
            ++failures;
        }
    }
}

MailWalkGateFacts Ready()
{
    MailWalkGateFacts f;
    f.hasBotAI = true;
    f.inWorld = true;
    f.alive = true;
    return f;
}

void TheGateNamesTheFirstWall()
{
    CheckText("a free bot may walk", MailWalkGate(Ready()), "");

    MailWalkGateFacts f = Ready();
    f.hasBotAI = false;
    CheckText("a human is not walked", MailWalkGate(f), R::NoBotAI);

    f = Ready();
    f.inWorld = false;
    CheckText("out of the world", MailWalkGate(f), R::NotInWorld);

    f = Ready();
    f.loggingOut = true;
    CheckText("logging out", MailWalkGate(f), R::LoggingOut);

    f = Ready();
    f.alive = false;
    CheckText("dead", MailWalkGate(f), R::Dead);

    f = Ready();
    f.inFlight = true;
    CheckText("on a taxi", MailWalkGate(f), R::InFlight);

    f = Ready();
    f.inCombat = true;
    CheckText("fighting", MailWalkGate(f), R::InCombat);

    f = Ready();
    f.inInstance = true;
    CheckText("in an instance", MailWalkGate(f), R::InInstance);

    f = Ready();
    f.onRoster = true;
    CheckText("a roster character is aimed, not walked", MailWalkGate(f), R::OnRoster);

    f = Ready();
    f.groupedFollower = true;
    CheckText("a follower is not walked", MailWalkGate(f), R::Follower);

    f = Ready();
    f.alreadyWalking = true;
    CheckText("one walk per bot", MailWalkGate(f), R::AlreadyWalking);

    f = Ready();
    f.heldByAnother = true;
    CheckText("another verb's hold is not taken over", MailWalkGate(f), R::HeldByAnother);

    // Combat outranks the roster: the row says the wall that moves first.
    f = Ready();
    f.inCombat = true;
    f.onRoster = true;
    CheckText("combat is named before the roster", MailWalkGate(f), R::InCombat);

    // Death outranks combat: a corpse is never "in combat" usefully.
    f = Ready();
    f.alive = false;
    f.inCombat = true;
    CheckText("death is named before combat", MailWalkGate(f), R::Dead);
}

void TheNearestUsableMailboxIsChosen()
{
    std::vector<MailboxCandidate> none;
    MailboxChoice c = ChooseNearestMailbox(none, 0.f, 0.f, 0.f, 600.f);
    CheckText("no box on the map", c.error, R::NoMailboxOnMap);
    Check("no index", c.index == -1, true);

    std::vector<MailboxCandidate> boxes = {
        {300.f, 0.f, 0.f, false},
        {100.f, 0.f, 0.f, false},
        {0.f, 200.f, 0.f, false},
    };
    c = ChooseNearestMailbox(boxes, 0.f, 0.f, 0.f, 600.f);
    CheckText("a box is chosen", c.error, "");
    CheckNumber("the nearest one", static_cast<uint64_t>(c.index), 1);
    CheckNear("its distance", c.yards, 100.f);
    CheckNear("the nearest of any", c.nearestYards, 100.f);

    // Height counts: a box straight overhead on another floor is further than
    // its footprint says.
    std::vector<MailboxCandidate> floors = {
        {10.f, 0.f, 60.f, false},
        {40.f, 0.f, 0.f, false},
    };
    c = ChooseNearestMailbox(floors, 0.f, 0.f, 0.f, 600.f);
    CheckNumber("the box on this floor", static_cast<uint64_t>(c.index), 1);

    // The other side's box is skipped, not chosen, even when it is nearer.
    std::vector<MailboxCandidate> hostile = {
        {50.f, 0.f, 0.f, true},
        {400.f, 0.f, 0.f, false},
    };
    c = ChooseNearestMailbox(hostile, 0.f, 0.f, 0.f, 600.f);
    CheckText("a friendly box further off", c.error, "");
    CheckNumber("skips the hostile one", static_cast<uint64_t>(c.index), 1);
    CheckNear("nearest of any still reports the hostile one", c.nearestYards, 50.f);

    std::vector<MailboxCandidate> onlyHostile = {{50.f, 0.f, 0.f, true}};
    c = ChooseNearestMailbox(onlyHostile, 0.f, 0.f, 0.f, 600.f);
    CheckText("only the other side's boxes", c.error, R::OtherSidesGround);
    Check("nothing chosen", c.index == -1, true);

    // Past the cap is refused by name, with the distance on the row.
    std::vector<MailboxCandidate> far = {{700.f, 0.f, 0.f, false}};
    c = ChooseNearestMailbox(far, 0.f, 0.f, 0.f, 600.f);
    CheckText("past the cap", c.error, R::MailboxTooFar);
    Check("nothing chosen past the cap", c.index == -1, true);
    CheckNear("the distance is still reported", c.yards, 700.f);

    // A row's own lower cap is honoured.
    c = ChooseNearestMailbox(boxes, 0.f, 0.f, 0.f, 50.f);
    CheckText("past a row's own cap", c.error, R::MailboxTooFar);
}

void EveryLegFitsThePathBudget()
{
    bool final = false;
    MailWalkPoint near = MailWalkLegAim(0.f, 0.f, 0.f, 100.f, 0.f, 5.f, 250.f, final);
    Check("a near box is the aim", final, true);
    CheckNear("near x", near.x, 100.f);
    CheckNear("near z", near.z, 5.f);

    MailWalkPoint leg = MailWalkLegAim(0.f, 0.f, 0.f, 500.f, 0.f, 50.f, 250.f, final);
    Check("a far box is a leg", final, false);
    CheckNear("the leg is 250 along", leg.x, 250.f);
    CheckNear("the leg stays on the line", leg.y, 0.f);
    CheckNear("the leg's z is interpolated", leg.z, 25.f);

    // Diagonal: the leg length is 2D, not per axis.
    MailWalkPoint diag = MailWalkLegAim(0.f, 0.f, 0.f, 300.f, 400.f, 0.f, 250.f, final);
    Check("a diagonal far box is a leg", final, false);
    CheckNear("diagonal x", diag.x, 150.f);
    CheckNear("diagonal y", diag.y, 200.f);

    // Every leg the verb aims is under what PathGenerator will smooth (296).
    Check("the leg length is under the smoothing budget", MAIL_WALK_LEG_YARDS < 296.f, true);
    Check("the cap needs at most three legs", MAIL_WALK_MAX_YARDS / MAIL_WALK_LEG_YARDS <= 3.f,
          true);
}

void TheLineIsSampledEndToEnd()
{
    std::vector<MailWalkPoint> pts = MailWalkLineSamples(0.f, 0.f, 90.f, 0.f, 30.f);
    CheckNumber("90 yards at 30 is five samples", pts.size(), 5);
    CheckNear("the first is the start", pts.front().x, 0.f);
    CheckNear("the last is the box", pts.back().x, 90.f);
    for (std::size_t i = 1; i < pts.size(); ++i)
        if (pts[i].x - pts[i - 1].x > 30.f + 0.01f)
        {
            std::printf("FAIL sample gap %zu is %.2f\n", i, pts[i].x - pts[i - 1].x);
            ++failures;
        }

    std::vector<MailWalkPoint> tiny = MailWalkLineSamples(0.f, 0.f, 5.f, 0.f, 30.f);
    CheckNumber("a short walk still reads both ends", tiny.size(), 2);

    std::vector<MailWalkPoint> zero = MailWalkLineSamples(0.f, 0.f, 500.f, 0.f, 0.f);
    CheckNumber("a zero spacing reads the ends only", zero.size(), 2);
}

void TheClockScalesWithTheWalkAndIsBounded()
{
    CheckNumber("a box beside you gets the floor", MailWalkTimeoutSeconds(0.f), 60);
    CheckNumber("35 yards", MailWalkTimeoutSeconds(35.f), 70);
    CheckNumber("350 yards", MailWalkTimeoutSeconds(350.f), 160);
    CheckNumber("the cap", MailWalkTimeoutSeconds(600.f), 231);
    CheckNumber("never past the ceiling", MailWalkTimeoutSeconds(5000.f), 300);
    CheckNumber("a negative reading is the floor", MailWalkTimeoutSeconds(-1.f), 60);
}

MailWalkFacts OnTheWay()
{
    MailWalkFacts f;
    f.present = true;
    f.alive = true;
    f.sameMap = true;
    f.timeoutMs = 100000;
    return f;
}

void AWalkEndsForTheRightReason()
{
    CheckText("still walking", MailWalkStateWord(JudgeMailWalk(OnTheWay())), "walking");

    MailWalkFacts f = OnTheWay();
    f.mailboxInReach = true;
    CheckText("arrived", MailWalkStateWord(JudgeMailWalk(f)), "arrived");
    CheckText("arrival has no refusal", MailWalkEndReason(JudgeMailWalk(f)), "");

    f = OnTheWay();
    f.present = false;
    CheckText("logged out", MailWalkEndReason(JudgeMailWalk(f)), R::LeftWorld);

    f = OnTheWay();
    f.alive = false;
    f.mailboxInReach = true;
    CheckText("a corpse at the box has not arrived", MailWalkEndReason(JudgeMailWalk(f)),
              R::Died);

    f = OnTheWay();
    f.inFlight = true;
    CheckText("a taxi ends the walk", MailWalkEndReason(JudgeMailWalk(f)), R::TookFlight);

    f = OnTheWay();
    f.sameMap = false;
    CheckText("a map change ends the walk", MailWalkEndReason(JudgeMailWalk(f)), R::LeftMap);

    // COMBAT OUTRANKS ARRIVAL. A character held at a mailbox through a fight is
    // a character killed by the hold, so the hold comes off.
    f = OnTheWay();
    f.inCombat = true;
    f.mailboxInReach = true;
    CheckText("combat at the box ends the walk", MailWalkEndReason(JudgeMailWalk(f)),
              R::EnteredCombat);

    // ARRIVAL OUTRANKS THE CLOCKS.
    f = OnTheWay();
    f.mailboxInReach = true;
    f.waitedMs = 200000;
    f.sinceProgressMs = 60000;
    f.groundRefusals = 10;
    CheckText("arriving late is arriving", MailWalkStateWord(JudgeMailWalk(f)), "arrived");

    f = OnTheWay();
    f.waitedMs = 100000;
    CheckText("the timeout", MailWalkEndReason(JudgeMailWalk(f)), R::TimedOut);

    f = OnTheWay();
    f.sinceProgressMs = MAIL_WALK_STALL_SECONDS * 1000u;
    CheckText("no progress", MailWalkEndReason(JudgeMailWalk(f)), R::Stalled);

    f = OnTheWay();
    f.sinceProgressMs = MAIL_WALK_STALL_SECONDS * 1000u - 1;
    CheckText("just short of a stall is walking", MailWalkStateWord(JudgeMailWalk(f)),
              "walking");

    f = OnTheWay();
    f.groundRefusals = MAIL_WALK_GROUND_REFUSALS_MAX;
    CheckText("ground refused poll after poll", MailWalkEndReason(JudgeMailWalk(f)),
              R::GroundRefused);
}

void ProgressIsTenYardsNearer()
{
    Check("ten yards nearer", MailWalkMadeProgress(100.f, 90.f), true);
    Check("nine yards nearer is not yet", MailWalkMadeProgress(100.f, 91.f), false);
    Check("further away is not", MailWalkMadeProgress(100.f, 120.f), false);
}

void WhichRefusalsMove()
{
    Check("combat moves", MailWalkRefusalRetryable(R::InCombat), true);
    Check("a walk under way moves", MailWalkRefusalRetryable(R::AlreadyWalking), true);
    Check("a timeout moves", MailWalkRefusalRetryable(R::TimedOut), true);
    Check("an instance moves", MailWalkRefusalRetryable(R::InInstance), true);
    Check("malformed does not", MailWalkRefusalRetryable(R::Malformed), false);
    Check("the roster does not", MailWalkRefusalRetryable(R::OnRoster), false);
    Check("too far does not", MailWalkRefusalRetryable(R::MailboxTooFar), false);
    Check("no box does not", MailWalkRefusalRetryable(R::NoMailboxOnMap), false);
    Check("the other side's ground does not",
          MailWalkRefusalRetryable(R::OtherSidesGround), false);
    Check("no bot AI does not", MailWalkRefusalRetryable(R::NoBotAI), false);
}

}  // namespace

int main()
{
    TheRowIsRecognisedByItsFirstWord();
    TheGrammarTakesOneOptionalCap();
    TheGateNamesTheFirstWall();
    TheNearestUsableMailboxIsChosen();
    EveryLegFitsThePathBudget();
    TheLineIsSampledEndToEnd();
    TheClockScalesWithTheWalkAndIsBounded();
    AWalkEndsForTheRightReason();
    ProgressIsTenYardsNearer();
    WhichRefusalsMove();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the mailbox walk decisions hold\n");
    return EXIT_SUCCESS;
}
