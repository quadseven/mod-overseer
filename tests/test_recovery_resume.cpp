/*
 * A recovery the module wrote down and has not applied is resumed after a
 * worldserver restart, not forgotten.
 *
 * This compiles against the pure decision file and nothing from AzerothCore,
 * and reads src/mod_overseer.cpp to pin the wiring (run from the repo root).
 *
 * WHAT WAS MEASURED ON THE DEV REALM, 2026-09-24. The Horde family's campaign
 * 12 failed its seventh attempt in a row at 10:47:59 ("GATHERING was refused:
 * the party is above the staging point, not near it"). overseer_run_recovery
 * row 46 (attempt 7) was written, and the bridge answered it hearth_regroup at
 * 10:48:07, to be applied after a 900-second backoff. The worldserver
 * restarted at 10:53. The campaign's run rows held six trailing failures,
 * because a refusal before GATHERING or BARRIER opens a run row writes none,
 * and attempt 6's recovery read 'applied'. So the IDLE gate opened a fresh
 * attempt at 11:11:07 (IDLE -> RESET -> GATHERING) and row 46 stayed
 * 'answered', applied NULL, for good.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace OverseerDecisions;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want = true)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckStep(char const* what, IdleCampaignPlan got, IdleCampaignStep want,
               unsigned wantAttempt)
{
    if (got.step == want && got.attempt == wantAttempt)
        return;
    std::printf("FAIL %s: got step %d attempt %u, want step %d attempt %u\n", what,
                static_cast<int>(got.step), got.attempt, static_cast<int>(want), wantAttempt);
    ++failures;
}

CampaignRecoveryRow Row(unsigned attempt, bool applied, bool openedSince = false)
{
    CampaignRecoveryRow row;
    row.present = true;
    row.attempt = attempt;
    row.applied = applied;
    row.attemptOpenedSince = openedSince;
    return row;
}

void TheMeasuredRestartResumesRow46()
{
    // Six trailing failures on the run rows; attempt 6's recovery applied;
    // the newest request row is attempt 7, answered and never applied.
    CheckStep("the answered attempt-7 row is resumed, not a fresh attempt opened",
              IdleCampaignRecovery(6, true, Row(7, false)),
              IdleCampaignStep::ResumeRecovery, 7);
    // The streak on the rows does not matter when the request row says more.
    CheckStep("even with no run row behind it",
              IdleCampaignRecovery(0, false, Row(1, false)),
              IdleCampaignStep::ResumeRecovery, 1);
}

void EverythingElseIsAsItWas()
{
    CampaignRecoveryRow none;
    CheckStep("no rows, no failures: the next attempt opens",
              IdleCampaignRecovery(0, false, none), IdleCampaignStep::OpenAttempt, 0);
    CheckStep("failures nobody asked about: a new request",
              IdleCampaignRecovery(3, false, none), IdleCampaignStep::EnterRecovery, 3);
    CheckStep("failures whose newest request was applied, not yet written for this "
              "streak: a new request",
              IdleCampaignRecovery(3, false, Row(2, true)), IdleCampaignStep::EnterRecovery, 3);
    CheckStep("the town trip's applied row opens the attempt it was for",
              IdleCampaignRecovery(4, true, Row(4, true)), IdleCampaignStep::OpenAttempt, 0);
}

void AStaleRowIsNotResumed()
{
    // What the bug left behind: an attempt was opened over the unapplied row.
    // That attempt is the answer now; the row is not resumed for ever after.
    CheckStep("a row an attempt was opened after is superseded",
              IdleCampaignRecovery(0, false, Row(7, false, true)),
              IdleCampaignStep::OpenAttempt, 0);
    CheckStep("and the run rows decide again",
              IdleCampaignRecovery(6, false, Row(7, false, true)),
              IdleCampaignStep::EnterRecovery, 6);
    CampaignRecoveryRow zero = Row(0, false);
    CheckStep("a row with no attempt is nothing to resume",
              IdleCampaignRecovery(0, false, zero), IdleCampaignStep::OpenAttempt, 0);
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
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
    auto has = [&](char const* what, char const* text) {
        Check(what, source.find(text) != std::string::npos);
    };
    has("the IDLE gate reads the newest request row",
        "NewestCampaignRecovery(leaderName, campaignId, rowFailure, rowAgo);");
    has("and asks the decision",
        "OverseerDecisions::IdleCampaignRecovery(");
    has("a resumed recovery writes no second row",
        "idlePlan.attempt, endedAgo, resume);");
    has("the resume returns before the request is written",
        "        if (resumed)\n        {");
    has("the newest row only, and only run_recovery",
        "\"WHERE r.leader_name = '{}' AND r.campaign_id = {} AND r.kind = 'run_recovery' \"\n"
        "            \"ORDER BY r.id DESC LIMIT 1\"");
    has("the answer on the resumed row is still the one read",
        "\"AND status = 'answered' ORDER BY id DESC LIMIT 1\"");
}

}  // namespace

int main()
{
    TheMeasuredRestartResumesRow46();
    EverythingElseIsAsItWas();
    AStaleRowIsNotResumed();
    TheAdapterIsWired();
    if (failures)
        std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
