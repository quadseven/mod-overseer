/*
 * The release of an ended raid attempt waits for the living inside, but not
 * forever (#1222).
 *
 * Once an attempt has ended, every poll releases again, and the release keeps
 * the raid while anybody is on the raid map. A living survivor (up to 10% can
 * be standing after a wipe, a feign-death hunter for one) was never walked out,
 * so the order stalled until that member died or left, and said so on every
 * poll. Two halves:
 *
 *   1. RaidReleaseDisbandsAnyway bounds the wait at RAID_RELEASE_WAIT_SECONDS
 *      from the poll whose release first did not finish.
 *   2. The adapter asks it inside ReleaseRaidRun, starts the clock where it
 *      marks the attempt as ending, and says the wait once, not every poll.
 */

#include "overseer_decisions.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace OverseerDecisions;

namespace
{
int g_failures = 0;

void Check(char const* what, bool ok)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

void TheDecision()
{
    std::time_t const t0 = 1000000;
    Check("the bound is positive and at most ten minutes",
          RAID_RELEASE_WAIT_SECONDS > 0 && RAID_RELEASE_WAIT_SECONDS <= 600);
    Check("no clock started: the release keeps waiting",
          !RaidReleaseDisbandsAnyway(0, t0));
    Check("the poll that started the clock waits", !RaidReleaseDisbandsAnyway(t0, t0));
    Check("one second short of the bound still waits",
          !RaidReleaseDisbandsAnyway(t0, t0 + RAID_RELEASE_WAIT_SECONDS - 1));
    Check("at the bound the raid is disbanded anyway",
          RaidReleaseDisbandsAnyway(t0, t0 + RAID_RELEASE_WAIT_SECONDS));
    Check("an hour on it is still disbanded, not waited on forever",
          RaidReleaseDisbandsAnyway(t0, t0 + 3600));
}

void TheAdapter(std::string const& text)
{
    std::string const release = Body(text, "bool ReleaseRaidRun(");
    Check("ReleaseRaidRun exists", !release.empty());
    std::size_t const asked = release.find(
        "OverseerDecisions::RaidReleaseDisbandsAnyway(run.attemptEndingSince");
    Check("ReleaseRaidRun asks RaidReleaseDisbandsAnyway on the attempt's clock",
          asked != std::string::npos);
    std::size_t const kept = release.find("return false;");
    Check("the bound is asked before the raid is kept",
          asked != std::string::npos && kept != std::string::npos && asked < kept);
    std::size_t const said = release.find("is still \"");
    std::size_t const once = release.find("if (!run.attemptEnding)");
    Check("the still-inside line is said only before the attempt is marked ending",
          said != std::string::npos && once != std::string::npos && once < said);

    std::string const drive = Body(text, "void DriveRaidRun()");
    Check("DriveRaidRun exists", !drive.empty());
    std::size_t const marked = drive.find("run.attemptEnding = true;");
    std::size_t const clock = drive.find("run.attemptEndingSince = std::time(nullptr);");
    Check("the clock starts where the attempt is marked as ending",
          marked != std::string::npos && clock != std::string::npos && clock > marked &&
              clock - marked < 200);
    Check("the release is given this poll's time",
          drive.find("ReleaseRaidRun(roster.family, run, door, std::time(nullptr))") !=
              std::string::npos);
}
}  // namespace

int main()
{
    TheDecision();

    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
    {
        std::cerr << "cannot read src/mod_overseer.cpp\n";
        return EXIT_FAILURE;
    }
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    TheAdapter(text);

    if (g_failures)
        return EXIT_FAILURE;
    std::cout << "an ended raid attempt waits at most " << RAID_RELEASE_WAIT_SECONDS
              << " s for the living inside, and says so once\n";
    return EXIT_SUCCESS;
}
