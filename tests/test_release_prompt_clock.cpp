/*
 * The release-prompt dead clock covers one unbroken dead spell
 * (mod-overseer#252).
 *
 * The defect: the stuck-revival drive set its "first seen dead on the release
 * prompt" time on the first sighting and ended it only on a release. A
 * character raised without releasing (a healer's resurrect) kept the clock,
 * so its next death was timed from the first one: the drive logged "has been
 * dead 368s" about a character whose death the core had booked two seconds
 * earlier, after a revive in between.
 *
 * Compiled against src/overseer_decisions.cpp; the last part reads
 * src/mod_overseer.cpp (run from the repo root) to pin the wiring.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

bool Has(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

constexpr std::int64_t POLL = 8;  // ENGAGEMENT_POLL_MS, in seconds
constexpr std::int64_t GAP = RELEASE_PROMPT_MAX_GAP_SECONDS;

// The drive's own book, the way DriveStuckRevival keeps it: one clock by
// name, erased when the character is seen alive.
struct Drive
{
    std::map<std::string, ReleasePromptClock> clocks;
    std::int64_t lastDeathAt{0};

    ReleasePromptReading Dead(std::int64_t now)
    {
        return ReleasePromptObserve(clocks["bork"], now, lastDeathAt, GAP);
    }
    void Alive() { clocks.erase("bork"); }
};

void AnUnbrokenSpellCountsFromTheFirstSighting()
{
    Drive d;
    d.lastDeathAt = 1000;
    ReleasePromptReading r = d.Dead(1002);
    Check("the first sighting starts the clock", r.start == ReleasePromptStart::FirstSighting);
    Check("and reads zero", r.deadSeconds == 0);
    for (std::int64_t t = 1002 + POLL; t <= 1002 + 6 * POLL; t += POLL)
        r = d.Dead(t);
    Check("polls every 8 s extend it", r.start == ReleasePromptStart::Running);
    Check("to six polls' worth", r.deadSeconds == 6 * POLL);
}

void ASecondDeathIsTimedFromItsOwnSighting()
{
    // Row 3230's death at 17:45:09, a revive, row 3231's death at 17:51:13,
    // and the watchdog at 17:51:16. Seconds since 17:45:00.
    Drive d;
    d.lastDeathAt = 9;
    d.Dead(9);
    for (std::int64_t t = 9 + POLL; t < 60; t += POLL)
        d.Dead(t);
    d.Alive();  // seen alive: a healer raised it without a release
    d.lastDeathAt = 373;
    ReleasePromptReading r = d.Dead(376);
    Check("a death after a revive starts a fresh clock", r.start == ReleasePromptStart::FirstSighting);
    Check("which reads 0, not 367", r.deadSeconds == 0);
    r = d.Dead(376 + POLL);
    Check("and then the age of the SECOND death", r.deadSeconds == POLL);
    Check("which is not yet worth a release",
          r.deadSeconds < 45 /* STUCK_REVIVAL_DEAD_SECONDS */);
}

void ADeathBookedSinceTheClockStartedRestartsIt()
{
    // The revive and the second death both fall between two polls, so no
    // poll ever sees the character alive. The death hook did see it die.
    Drive d;
    d.lastDeathAt = 100;
    d.Dead(101);
    d.Dead(109);
    d.lastDeathAt = 114;
    ReleasePromptReading r = d.Dead(117);
    Check("a death newer than the clock restarts it", r.start == ReleasePromptStart::NewDeath);
    Check("timed from this sighting", r.deadSeconds == 0);
    r = d.Dead(125);
    Check("and the same death does not restart it again", r.start == ReleasePromptStart::Running);
    Check("so it counts on from the restart", r.deadSeconds == POLL);
}

void TheDeathThatStartedTheClockDoesNotRestartIt()
{
    Drive d;
    d.lastDeathAt = 200;
    d.Dead(200);  // hook and first sighting in the same second
    ReleasePromptReading r = d.Dead(200 + POLL);
    Check("the stamp of the death being timed is not a new death",
          r.start == ReleasePromptStart::Running && r.deadSeconds == POLL);
}

void AGapInTheSightingsIsReportedNotCounted()
{
    Drive d;
    d.Dead(500);
    d.Dead(508);
    ReleasePromptReading r = d.Dead(508 + GAP + 1);
    Check("a sighting past the gap restarts the clock", r.start == ReleasePromptStart::AfterGap);
    Check("and says how long it was not looking", r.gapSeconds == GAP + 1);
    Check("without counting the gap as dead time", r.deadSeconds == 0);
    r = d.Dead(508 + GAP + 1 + POLL);
    Check("and runs on from there", r.start == ReleasePromptStart::Running && r.deadSeconds == POLL);
}

void AGapExactlyAtTheLimitIsStillUnbroken()
{
    Drive d;
    d.Dead(700);
    ReleasePromptReading r = d.Dead(700 + GAP);
    Check("a sighting exactly at the gap limit extends the run",
          r.start == ReleasePromptStart::Running && r.deadSeconds == GAP);
}

std::string Body(std::string const& source, std::string const& from, std::string const& to)
{
    std::size_t const at = source.find(from);
    if (at == std::string::npos)
        return "";
    std::size_t const end = source.find(to, at);
    return source.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

void TheDriveIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }

    std::string const drive =
        Body(source, "void DriveStuckRevival()", "AN OFFER ON THE TABLE IS ANSWERED");
    Check("DriveStuckRevival's alive branch was found", !drive.empty());
    std::string const alive = Body(drive, "if (bot->IsAlive())", "continue;\n            }");
    Check("the alive branch ends the release-prompt clock",
          Has(alive, "_awaitingRelease.erase(name);"));

    std::string const prompt = Body(source, "if (!ghostTime)", "// Alive again, or released");
    Check("the release-prompt block was found", !prompt.empty());
    Check("it reads the clock through ReleasePromptObserve",
          Has(prompt, "ReleasePromptObserve(") && Has(prompt, "LastDeathAt(name)"));
    Check("it times the release from the reading, not a raw first sighting",
          Has(prompt, "reading.deadSeconds < STUCK_REVIVAL_DEAD_SECONDS") &&
              !Has(prompt, "now - seen->second"));
    Check("a restarted clock never releases on the same poll",
          Has(prompt, "if (reading.start != OverseerDecisions::ReleasePromptStart::Running)\n"
                      "                    continue;"));

    std::string const hook = Body(source, "void OnPlayerJustDied(Player* player) override",
                                  "\n    }\n");
    Check("the death hook stamps roster deaths", Has(hook, "StampDeath(player->GetName())"));
}

}  // namespace

int main()
{
    AnUnbrokenSpellCountsFromTheFirstSighting();
    ASecondDeathIsTimedFromItsOwnSighting();
    ADeathBookedSinceTheClockStartedRestartsIt();
    TheDeathThatStartedTheClockDoesNotRestartIt();
    AGapInTheSightingsIsReportedNotCounted();
    AGapExactlyAtTheLimitIsStillUnbroken();
    TheDriveIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_release_prompt_clock: all passed\n");
    return 0;
}
