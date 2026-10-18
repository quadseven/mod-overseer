/*
 * An unreachable errand the travel drive let go of is not granted again on the
 * next poll (#722).
 *
 * The live failure this pins, from the dev realm's worldserver log on
 * 2026-10-18 (17:00-17:10 UTC, one family leader, all 119 lines identical):
 *
 *   leader intent '<leader>' -> economy by travel column 'auctioneer'
 *       (he was free) - walks to 'auctioneer'
 *   '<leader>' was sent to 'auctioneer' and 5 of them are on this map and this
 *       character may interact with none of them ... 6 farther usable
 *       counter(s) are beyond the 1500-yard limit for a town-service walk
 *       on map 1 - releasing the errand
 *   leader intent '<leader>' ends economy by travel column (failed) after 0s
 *   travel release for '<leader>' skipped the column write - errand
 *       'auctioneer' is outstanding and this book did not claim the aim it
 *       would have erased
 *
 * every five seconds. The release skips the column write for a bridge aim on
 * purpose, and the landing (#558) is what was meant to stop the next poll from
 * reading the same standing aim as a new errand. #814 turned the landing's
 * "the book holds no claim" into "the claim matches the column", so no release
 * has landed since. Restored here, with the poison-aim breaker's points (#824)
 * kept polled.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else; the last check
 * reads src/mod_overseer.cpp to pin that the release and the travel drive ask
 * these decisions.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::LandedErrand;
using OverseerDecisions::LandedErrandStep;
using OverseerDecisions::ReleaseLandingFacts;
using OverseerDecisions::ReleaseLandsAim;
using OverseerDecisions::UnresolvedAimStandsDown;

namespace
{

int failures = 0;

int64_t const CEILING = 15 * 60;

void Check(char const* what, bool got)
{
    if (got)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

ReleaseLandingFacts Facts(bool anyClaim, std::string const& driving,
                          std::string const& standing)
{
    ReleaseLandingFacts facts;
    facts.anyClaim = anyClaim;
    facts.driving = driving;
    facts.standing = standing;
    return facts;
}

// THE MEASURED LOOP, REPLAYED. A bridge aim the drive was walking, no claim,
// released unresolved: it lands, and the polls after it stand down instead of
// granting the errand again. 120 polls at five seconds is the ten-minute
// window; the measured count was one grant per poll.
void AnUnreachableBridgeAimLandsAndIsNotGrantedEveryPoll()
{
    std::string const aim = "auctioneer";
    bool const lands = ReleaseLandsAim(Facts(false, aim, aim)) && UnresolvedAimStandsDown(aim);
    Check("an unclaimed auctioneer aim the drive was walking lands", lands);

    int grants = 1;  // the poll that granted it and released it in 0 s
    std::string landed = lands ? aim : std::string();
    int64_t landedAt = 0;
    for (int64_t now = 5; now <= 600; now += 5)
    {
        LandedErrand const step = LandedErrandStep(landed, aim, false, false,
                                                   now - landedAt, CEILING);
        if (step == LandedErrand::StandDown)
            continue;
        // Granted again, and released again in the same second.
        ++grants;
        landed = ReleaseLandsAim(Facts(false, aim, aim)) ? aim : std::string();
        landedAt = now;
    }
    if (grants != 1)
        std::printf("  grants in the ten-minute window: %d\n", grants);
    Check("one grant in ten minutes, not one per poll", grants == 1);
}

// THE CEILING IS THE COOLDOWN, AND A REWRITTEN COLUMN ENDS IT AT ONCE. A far
// trip the bridge aims at one counter's creature entry (wow-overseer#1179) is a
// different aim and is walked on the next poll.
void TheLandingEndsOnTheCeilingOrANewAim()
{
    Check("still standing down just inside the ceiling",
          LandedErrandStep("auctioneer", "auctioneer", false, false, CEILING - 5, CEILING) ==
              LandedErrand::StandDown);
    Check("tried again once the ceiling passes",
          LandedErrandStep("auctioneer", "auctioneer", false, false, CEILING, CEILING) ==
              LandedErrand::Resume);
    Check("a creature entry written over it is walked at once",
          LandedErrandStep("auctioneer", "8719", false, false, 5, CEILING) ==
              LandedErrand::Resume);
}

// #814'S INVERSION, PINNED BOTH WAYS. A claim of any kind means no landing: a
// claimed aim's release clears the column (#822), and a claim the column moved
// away from leaves a newer writer's aim that the drive must still start.
void OnlyAnAimTheBookNeverClaimedLands()
{
    Check("a claim matching the column does not land",
          !ReleaseLandsAim(Facts(true, "repair", "repair")));
    Check("a claim the column moved away from does not land",
          !ReleaseLandsAim(Facts(true, "at:0:1,2,3", "vendor")));
    Check("an aim the drive was not walking does not land",
          !ReleaseLandsAim(Facts(false, "", "vendor")));
    Check("an aim other than the one it was walking does not land",
          !ReleaseLandsAim(Facts(false, "banker", "vendor")));
    Check("an empty column does not land", !ReleaseLandsAim(Facts(false, "", "")));
    char const* const aims[] = {"vendor", "banker", "repair", "auctioneer", "at:1:10,20,30"};
    for (char const* aim : aims)
        Check(aim, ReleaseLandsAim(Facts(false, aim, aim)));
}

// A POINT IS LEFT TO THE POISON-AIM BREAKER (#824), which counts refusals of an
// unclaimed `at:` aim poll by poll and needs 20 of them over five minutes with
// no gap over two. A landing after the first would starve it.
void APointIsLeftToThePoisonBreaker()
{
    Check("a point does not stand down", !UnresolvedAimStandsDown("at:36:-16,-383,61"));
    char const* const aims[] = {"auctioneer", "vendor", "banker", "repair", "8719",
                                "trigger:1234"};
    for (char const* aim : aims)
        Check(aim, UnresolvedAimStandsDown(aim));
    Check("an empty aim does not stand down", !UnresolvedAimStandsDown(""));

    OverseerDecisions::PoisonAimState state;
    bool tripped = false;
    for (int64_t now = 0; now <= 300 && !tripped; now += 15)
        tripped = OverseerDecisions::PoisonAimStep(state, "at:36:-16,-383,61", false, 1, now);
    Check("polled every fifteen seconds, the breaker still trips", tripped);
}

std::string Source()
{
    std::ifstream file("src/mod_overseer.cpp");
    std::stringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

// THE MODULE ASKS THESE DECISIONS. The release's landing reads any claim, not a
// matching one, and the unresolved-aim release drops only a point's landing.
void TheModuleAsksTheDecisions()
{
    std::string const source = Source();
    Check("module source is readable", !source.empty());
    std::size_t const release = source.find("void ReleaseImpl(std::string const& name");
    Check("the travel release exists", release != std::string::npos);
    if (release == std::string::npos)
        return;
    std::string const body = source.substr(release, 9000);
    Check("the landing is ReleaseLandsAim's",
          body.find("bool const wasDriving = OverseerDecisions::ReleaseLandsAim(") !=
              std::string::npos);
    Check("the landing reads any claim the book holds",
          body.find("landingFacts.anyClaim = claimedAim != _claimed.end();") !=
              std::string::npos);
    Check("the inverted test is gone",
          body.find("wasDriving = claimed &&") == std::string::npos);

    std::size_t const unresolved =
        source.find("_travelAims.Release(name, \"the travel drive (no such spawn on this map)\");");
    Check("the unresolved-aim release exists", unresolved != std::string::npos);
    if (unresolved == std::string::npos)
        return;
    std::string const after = source.substr(unresolved, 1200);
    std::size_t const asks = after.find("if (!OverseerDecisions::UnresolvedAimStandsDown(target))");
    std::size_t const forgets = after.find("_travelAims.ForgetLanding(name);");
    Check("a point's landing is dropped after the unresolved-aim release",
          asks != std::string::npos && forgets != std::string::npos && forgets > asks);
}

}  // namespace

int main()
{
    AnUnreachableBridgeAimLandsAndIsNotGrantedEveryPoll();
    TheLandingEndsOnTheCeilingOrANewAim();
    OnlyAnAimTheBookNeverClaimedLands();
    APointIsLeftToThePoisonBreaker();
    TheModuleAsksTheDecisions();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("unreachable errand landing: all checks passed\n");
    return EXIT_SUCCESS;
}
