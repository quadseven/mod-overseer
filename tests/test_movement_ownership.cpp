/*
 * Characterization tests for the Movement Ownership module (#1312).
 *
 * Who may move a character is one concept: claim, release and hold of the
 * travel column by a named owner, the hand-back clock, the fences that keep a
 * website or profession errand from being overwritten, and the exact-aim
 * release a dungeon run makes when it ends. Bugs #812, #813, #814 lived in how
 * those rules were called, so they were pinned by substring. This file drives
 * the rules through the module's interface instead, against the in-memory
 * adapter, and names the sequence each one guards.
 *
 * The last case reads src/mod_overseer.cpp: the module is the one home of
 * these rules only while no code outside its roster adapter writes the column.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_movement_ownership.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::TravelOwner;
using OverseerMovement::ClaimAnswer;
using OverseerMovement::InMemoryAimColumn;
using OverseerMovement::MovementOwnership;
using OverseerMovement::ReleaseResult;
using OverseerMovement::RunAimRetire;

namespace
{

int failures = 0;
constexpr time_t GRACE = 45;
constexpr time_t T0 = 1000000;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

struct Rig
{
    InMemoryAimColumn column;
    MovementOwnership wheel{column, GRACE};
    unsigned leaderAsks{0};
    bool leaderFree{true};

    OverseerMovement::ClaimResult Claim(std::string const& name, std::string const& aim,
                                        TravelOwner owner, bool cached = false)
    {
        return wheel.Claim(name, aim, owner, cached, [this] {
            ++leaderAsks;
            return leaderFree;
        });
    }
};

// A named owner takes an empty column; the claim is remembered under that
// owner; taking the wheel never stamps the hand-back clock.
void AClaimTakesTheWheelForItsOwner()
{
    Rig r;
    auto const got = r.Claim("Ann", "at:0:1,2,3", TravelOwner::CatchUp);
    Check("claim over an empty column writes", got.answer == ClaimAnswer::Written && got.Holds());
    Check("the column holds the aim", r.column.columns["Ann"] == "at:0:1,2,3");
    TravelOwner owner = TravelOwner::TownTrip;
    Check("the claim names its owner",
          r.wheel.ClaimedBy("Ann", "at:0:1,2,3", owner) && owner == TravelOwner::CatchUp);
    Check("the claim is held for that aim only",
          r.wheel.HoldsClaim("Ann", "at:0:1,2,3") && !r.wheel.HoldsClaim("Ann", "vendor"));
    Check("a claim does not stamp the hand-back clock",
          !r.wheel.WithinHandbackGrace("Ann", T0) &&
              !r.wheel.WithinHandbackGrace("Ann", std::time(nullptr)));
}

// Release blanks the claimed aim, forgets the claim, and stamps the clock that
// keeps the quest drive off the wheel for exactly the grace.
void AReleaseHandsTheWheelBackOnAClock()
{
    Rig r;
    r.Claim("Ann", "vendor", TravelOwner::TownTrip);
    ReleaseResult const got = r.wheel.Release("Ann", T0);
    Check("a claimed aim is cleared", r.column.columns["Ann"].empty() && got.fence.empty());
    Check("the release says which aim ended", got.endedAim == "vendor" && got.anyClaim);
    Check("the claim is forgotten", !r.wheel.HoldsClaim("Ann", "vendor"));
    Check("held inside the grace", r.wheel.WithinHandbackGrace("Ann", T0 + GRACE - 1));
    Check("free once the grace lapses", !r.wheel.WithinHandbackGrace("Ann", T0 + GRACE));
    Check("and the lapsed entry is swept", !r.wheel.WithinHandbackGrace("Ann", T0));

    // A release with nothing outstanding still stamps: unconditional.
    r.wheel.Release("Bob", T0);
    Check("an empty release stamps too", r.wheel.WithinHandbackGrace("Bob", T0 + 1));

    // Claiming again after the grace leaves the clock unstamped.
    r.Claim("Ann", "banker", TravelOwner::TownTrip);
    Check("only a release stamps the clock", !r.wheel.WithinHandbackGrace("Ann", T0 + GRACE));
}

// A website errand (economy keyword or positional aim) is foreign: no claim
// overwrites it and no release erases it.
void AForeignAimIsLeftAlone()
{
    for (char const* foreign : {"vendor", "banker", "at:0:4,5,6"})
    {
        Rig r;
        r.column.columns["Ann"] = foreign;
        unsigned const before = r.column.writes;
        auto const got = r.Claim("Ann", "at:0:1,2,3", TravelOwner::CatchUp);
        Check("a claim over a foreign errand is fenced",
              got.answer == ClaimAnswer::Fenced &&
                  got.verdict == OverseerDecisions::TravelClaim::RefusedForeign);
        Check("and writes nothing", r.column.writes == before && r.column.columns["Ann"] == foreign);
        Check("the leader is not asked about a fenced claim", r.leaderAsks == 0);
        Check("the forecast agrees",
              r.wheel.FenceRefuses("Ann", "at:0:1,2,3", TravelOwner::CatchUp, false));
        ReleaseResult const rel = r.wheel.Release("Ann", T0);
        Check("a release keeps the foreign errand",
              !rel.fence.empty() && r.column.columns["Ann"] == foreign &&
                  r.column.writes == before);
    }
}

// A profession errand (learn_skill set) outranks a home errand even over an
// empty column; a catch-up over an empty column overwrites no trainer walk.
void AProfessionErrandIsFenced()
{
    Rig r;
    r.column.learnSkills["Ann"] = 186;
    auto const home = r.Claim("Ann", "home", TravelOwner::HomeErrand);
    Check("the home errand is fenced by the trainer trip",
          home.answer == ClaimAnswer::Fenced &&
              home.verdict == OverseerDecisions::TravelClaim::RefusedProfession &&
              home.learnSkill == 186);
    Check("nothing written", r.column.writes == 0);
    auto const fetch = r.Claim("Ann", "at:0:1,2,3", TravelOwner::CatchUp);
    Check("a catch-up over an empty column passes", fetch.answer == ClaimAnswer::Written);

    // An unclaimed column with a trainer trip pending is not erased.
    Rig s;
    s.column.learnSkills["Ann"] = 186;
    s.column.columns["Ann"] = "trainer";
    ReleaseResult const rel = s.wheel.Release("Ann", T0);
    Check("a release keeps an unclaimed trainer errand",
          !rel.fence.empty() && s.column.columns["Ann"] == "trainer");
}

// A live run outranks a positional walk; a training stop preempts a town
// errand. Both are writes.
void OutrankingOwnersWrite()
{
    Rig r;
    r.column.columns["Ann"] = "at:0:4,5,6";
    auto const run = r.Claim("Ann", "at:0:1,2,3", TravelOwner::Run);
    Check("a live run outranks a positional walk",
          run.answer == ClaimAnswer::Written &&
              run.verdict == OverseerDecisions::TravelClaim::Outrank &&
              r.column.columns["Ann"] == "at:0:1,2,3");
    Rig s;
    s.column.columns["Ann"] = "vendor";
    auto const stop = s.Claim("Ann", "trainer", TravelOwner::TrainingStop);
    Check("a training stop preempts a town errand",
          stop.answer == ClaimAnswer::Written &&
              stop.verdict == OverseerDecisions::TravelClaim::Preempt);
}

// The column moved after the claim: what stands now is somebody else's.
void AChangedColumnIsNotErasedByTheStaleClaim()
{
    Rig r;
    r.Claim("Ann", "vendor", TravelOwner::TownTrip);
    r.column.columns["Ann"] = "banker";  // the website re-aimed
    ReleaseResult const rel = r.wheel.Release("Ann", T0);
    Check("the newer errand survives the stale release",
          !rel.fence.empty() && r.column.columns["Ann"] == "banker");
    Check("the stale claim is gone", !r.wheel.HoldsClaim("Ann", "vendor"));
}

// #822: a self-claimed maintenance aim whose column has not moved clears.
void ASelfClaimedMaintenanceAimClears()
{
    Rig r;
    r.Claim("Ann", "repair", TravelOwner::TownTrip);
    ReleaseResult const rel = r.wheel.Release("Ann", T0);
    Check("a self-claimed repair aim clears",
          rel.fence.empty() && r.column.columns["Ann"].empty());
}

// RUN ENDS WHILE A NEWER AIM STANDS (#812, #813, #814, bcc6c83). The run's
// release must not end, reset or forget the newer owner's errand.
void ARunEndingLeavesANewerAimStanding()
{
    Rig r;
    r.Claim("Lead", "at:0:1,2,3", TravelOwner::Run);
    // The fetch replaces the module's own staging aim with its own.
    auto const fetch = r.Claim("Lead", "at:0:7,8,9", TravelOwner::CatchUp);
    Check("a newer owner takes over the module's own aim", fetch.answer == ClaimAnswer::Written);
    unsigned const before = r.column.writes;
    bool may = true;
    ReleaseResult const rel = r.wheel.ReleaseRunAim("Lead", "at:0:1,2,3", T0, may);
    Check("the run's release leaves the newer aim",
          rel.leftNewerAim && r.column.columns["Lead"] == "at:0:7,8,9" &&
              r.column.writes == before);
    TravelOwner owner = TravelOwner::Run;
    Check("the newer owner keeps its claim",
          r.wheel.ClaimedBy("Lead", "at:0:7,8,9", owner) && owner == TravelOwner::CatchUp);
    Check("no hand-back is stamped for an errand that did not end",
          !r.wheel.WithinHandbackGrace("Lead", T0));
    Check("the run may not retire the recorded leg", !may);
    Check("so the exact clear is not tried",
          r.wheel.RetireRunAim("Lead", "at:0:1,2,3", may).answer == RunAimRetire::NotTried &&
              r.column.columns["Lead"] == "at:0:7,8,9");

    // Same aim, newer non-run owner: still not the run's to end.
    Rig s;
    s.Claim("Lead", "at:0:1,2,3", TravelOwner::CatchUp);
    bool sMay = true;
    ReleaseResult const sRel = s.wheel.ReleaseRunAim("Lead", "at:0:1,2,3", T0, sMay);
    Check("a non-run claim on the same aim is left", sRel.leftNewerAim && !sMay &&
                                                         s.column.columns["Lead"] == "at:0:1,2,3");
}

// The run ends on its own leg: cleared, claim gone, clock stamped.
void ARunEndingOnItsOwnLegClears()
{
    for (TravelOwner runOwner : {TravelOwner::Run, TravelOwner::WalkBackIn, TravelOwner::Exit})
    {
        Rig r;
        r.Claim("Lead", "at:0:1,2,3", runOwner);
        bool may = false;
        ReleaseResult const rel = r.wheel.ReleaseRunAim("Lead", "at:0:1,2,3", T0, may);
        Check("the run's own leg is released",
              !rel.leftNewerAim && may && r.column.columns["Lead"].empty());
        Check("and the wheel handed back", r.wheel.WithinHandbackGrace("Lead", T0 + 1));
    }
}

// A restart lost the claim: the run's leg now reads as a foreign positional
// aim, so Release protects it, and the exact retire clears only that value.
void ARunWhoseClaimARestartLostRetiresItsExactLeg()
{
    Rig r;
    r.column.columns["Lead"] = "at:0:1,2,3";
    bool may = false;
    ReleaseResult const rel = r.wheel.ReleaseRunAim("Lead", "at:0:1,2,3", T0, may);
    Check("release protects the unclaimed positional aim",
          !rel.leftNewerAim && !rel.fence.empty() && r.column.columns["Lead"] == "at:0:1,2,3");
    Check("but the run may retire its recorded leg", may);
    auto const retired = r.wheel.RetireRunAim("Lead", "at:0:1,2,3", may);
    Check("the exact leg is retired",
          retired.answer == RunAimRetire::Retired && retired.after.empty() &&
              r.column.columns["Lead"].empty());

    // A keyword errand is never a run leg, even when recorded.
    Rig s;
    s.column.columns["Lead"] = "vendor";
    Check("a keyword aim is not retired",
          s.wheel.RetireRunAim("Lead", "vendor", true).answer == RunAimRetire::NotTried &&
              s.column.columns["Lead"] == "vendor");
    // A disabled row is not touched by the synchronous clear.
    Rig t;
    t.column.columns["Lead"] = "at:0:1,2,3";
    t.column.disabled.insert("Lead");
    auto const left = t.wheel.RetireRunAim("Lead", "at:0:1,2,3", true);
    Check("a disabled row keeps its aim and says so",
          left.answer == RunAimRetire::NewerAimLeft && left.after == "at:0:1,2,3");
}

// A cached drive is trusted only while the column still holds it.
void ACachedDriveIsTrustedOnlyWhileTheColumnAgrees()
{
    Rig r;
    r.column.columns["Ann"] = "vendor";
    r.leaderFree = false;
    auto const same = r.Claim("Ann", "vendor", TravelOwner::TownTrip, true);
    Check("an unchanged cached drive holds without a write or a leader ask",
          same.answer == ClaimAnswer::AlreadyDriving && same.Holds() && r.column.writes == 0 &&
              r.leaderAsks == 0);
    Check("the forecast agrees", !r.wheel.FenceRefuses("Ann", "vendor", TravelOwner::TownTrip, true));
    for (char const* changed : {"banker", "at:0:1,2,3", "guard:0:1,2,3"})
    {
        r.column.columns["Ann"] = changed;
        Check("a changed column sends the cached drive through the fences",
              r.Claim("Ann", "vendor", TravelOwner::TownTrip, true).answer == ClaimAnswer::Fenced &&
                  r.wheel.FenceRefuses("Ann", "vendor", TravelOwner::TownTrip, true));
    }
}

// A leader held by another intent: the fences pass, but nothing is written.
void ABusyLeaderIsNotWritten()
{
    Rig r;
    r.leaderFree = false;
    auto const got = r.Claim("Lead", "vendor", TravelOwner::TownTrip);
    Check("a busy leader refuses the claim",
          got.answer == ClaimAnswer::LeaderBusy && !got.Holds() && r.leaderAsks == 1);
    Check("and leaves column and claim untouched",
          r.column.writes == 0 && !r.wheel.HoldsClaim("Lead", "vendor"));
    Check("the forecast does not ask whose turn it is",
          !r.wheel.FenceRefuses("Lead", "vendor", TravelOwner::TownTrip, false));
}

// An aim wider than the column is refused before anything is read (#1139).
void AnAimTooWideIsRefusedFirst()
{
    Rig r;
    std::string const wide(33, 'x');
    auto const got = r.Claim("Ann", wide, TravelOwner::TownTrip);
    Check("too wide is refused", got.answer == ClaimAnswer::TooLong);
    Check("before any read or write", r.column.reads == 0 && r.column.writes == 0);
    Check("the forecast predicts it", r.wheel.FenceRefuses("Ann", wide, TravelOwner::TownTrip, false));
}

// A leftover and a poisoned aim are cleared by compare-and-swap only.
void LeftoverAndPoisonedAimsUseTheExactClear()
{
    Rig r;
    r.Claim("Ann", "at:0:1,2,3", TravelOwner::CatchUp);
    r.wheel.ForgetLeftover("Ann", "at:0:1,2,3");
    Check("a leftover is cleared and its claim forgotten",
          r.column.columns["Ann"].empty() && !r.wheel.HoldsClaim("Ann", "at:0:1,2,3"));
    r.column.columns["Bob"] = "at:0:9,9,9";
    r.wheel.ForgetLeftover("Bob", "at:0:1,2,3");
    Check("a rewritten column is left", r.column.columns["Bob"] == "at:0:9,9,9");
    r.wheel.ClearPoisonedAim("Bob", "at:0:9,9,9");
    Check("a poisoned aim is cleared", r.column.columns["Bob"].empty());
    r.column.columns["Cy"] = "at:0:9,9,9";
    r.column.disabled.insert("Cy");
    r.wheel.ClearPoisonedAim("Cy", "at:0:9,9,9");
    Check("a disabled row is left", r.column.columns["Cy"] == "at:0:9,9,9");
}

// The column emptied outside the module: the claim goes and the clock stamps.
void AVanishedAimIsAReleaseToo()
{
    Rig r;
    r.Claim("Ann", "vendor", TravelOwner::TownTrip);
    unsigned const before = r.column.writes;
    r.wheel.ReleaseVanished("Ann", T0);
    Check("nothing is written", r.column.writes == before);
    Check("the claim is gone and the clock stamped",
          !r.wheel.HoldsClaim("Ann", "vendor") && r.wheel.WithinHandbackGrace("Ann", T0 + 1));
}


// THE ONE DOOR. Every UPDATE of `travel_npc` in the adapter lives inside
// RosterAimColumn, the module's production adapter, and the travel book holds
// the module rather than claim or hand-back maps of its own.
void OnlyTheRosterAdapterWritesTheColumn()
{
    std::ifstream file("src/mod_overseer.cpp");
    std::stringstream contents;
    contents << file.rdbuf();
    std::string const source = contents.str();
    Check("module source is readable", !source.empty());
    std::size_t const adapter =
        source.find("class RosterAimColumn final : public OverseerMovement::AimColumn");
    std::size_t const adapterEnd =
        adapter == std::string::npos ? adapter : source.find("\n};\n", adapter);
    Check("the roster adapter exists",
          adapter != std::string::npos && adapterEnd != std::string::npos);
    if (adapter == std::string::npos || adapterEnd == std::string::npos)
        return;
    unsigned inside = 0;
    for (std::size_t at = source.find("SET travel_npc"); at != std::string::npos;
         at = source.find("SET travel_npc", at + 1))
    {
        if (at > adapter && at < adapterEnd)
            ++inside;
        else
        {
            std::printf("FAIL a travel_npc write outside RosterAimColumn at offset %zu\n", at);
            ++failures;
        }
    }
    Check("the adapter holds the write and both clears", inside == 3);
    std::size_t const book = source.find("class TravelAimBook\n{");
    std::size_t const bookEnd = book == std::string::npos ? book : source.find("\n};\n", book);
    Check("the travel book exists", book != std::string::npos && bookEnd != std::string::npos);
    if (book == std::string::npos || bookEnd == std::string::npos)
        return;
    std::string const body = source.substr(book, bookEnd - book);
    Check("the travel book holds the module",
          body.find("OverseerMovement::MovementOwnership _wheel{_roster, "
                    "TRAVEL_HANDBACK_SECONDS};") != std::string::npos);
    Check("and no claim or hand-back map of its own",
          body.find("std::map<std::string, std::string> _claimed;") == std::string::npos &&
              body.find("_claimOwner") == std::string::npos &&
              body.find("std::map<std::string, time_t> _handback;") == std::string::npos);
}

}  // namespace

int main()
{
    AClaimTakesTheWheelForItsOwner();
    AReleaseHandsTheWheelBackOnAClock();
    AForeignAimIsLeftAlone();
    AProfessionErrandIsFenced();
    OutrankingOwnersWrite();
    AChangedColumnIsNotErasedByTheStaleClaim();
    ASelfClaimedMaintenanceAimClears();
    ARunEndingLeavesANewerAimStanding();
    ARunEndingOnItsOwnLegClears();
    ARunWhoseClaimARestartLostRetiresItsExactLeg();
    ACachedDriveIsTrustedOnlyWhileTheColumnAgrees();
    ABusyLeaderIsNotWritten();
    AnAimTooWideIsRefusedFirst();
    LeftoverAndPoisonedAimsUseTheExactClear();
    AVanishedAimIsAReleaseToo();
    OnlyTheRosterAdapterWritesTheColumn();
    if (failures)
    {
        std::printf("test_movement_ownership: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("test_movement_ownership: all cases passed\n");
    return EXIT_SUCCESS;
}
