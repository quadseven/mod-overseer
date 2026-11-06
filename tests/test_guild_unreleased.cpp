/*
 * A natural guild member that nothing releases (wow-overseer#1879).
 *
 * The live failure this pins: a level 20 guild hunter died to a city guard
 * on 2026-10-26 at 01:37 and was still a corpse on 2026-11-06, 262 hours
 * later: online, health 0, no ghost flag, no later death row. In nine minutes
 * of the world log no drive named it once.
 *
 * The guild ghost drive left an unreleased guild corpse to "the dead engine"
 * (StrandedGhostNext: "alive or not yet released (the dead engine releases
 * it)"), and nothing stood behind that engine. The family has had a backstop
 * since #252: DriveStuckRevival releases a character that has
 * stood on the release prompt for STUCK_REVIVAL_DEAD_SECONDS. A guild member
 * had none, so when the engine did not release one, it stayed a corpse until
 * somebody read the health page.
 *
 * The same drive also had no answer for a member that reads as alive at zero
 * health: no regeneration runs in combat, the natural guild's refresh no
 * longer sets full health, and every recovery path keys on the death state.
 * That state is not one the game can leave by itself either.
 *
 * GuildBodyNext decides; the drive asks it once per poll. Compiled against
 * src/overseer_decisions.cpp and nothing else, and reads src/mod_overseer.cpp
 * to pin the wiring (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::GUILD_RELEASE_GRACE_SECONDS;
using OverseerDecisions::GuildBodyFacts;
using OverseerDecisions::GuildBodyNext;
using OverseerDecisions::GuildBodyStep;
using OverseerDecisions::GuildBodyStepWord;

namespace
{

int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void Expect(char const* what, GuildBodyFacts const& facts, GuildBodyStep want)
{
    GuildBodyStep const got = GuildBodyNext(facts);
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, GuildBodyStepWord(got),
                GuildBodyStepWord(want));
    ++failures;
}

// The measured member: dead, not released, watched without a break for longer
// than the grace, in no run.
GuildBodyFacts TheHunterAtTheGuards()
{
    GuildBodyFacts f;
    f.alive = false;
    f.ghost = false;
    f.health = 0;
    f.clockRunning = true;
    f.seconds = 262 * 3600;
    f.runTakesBack = false;
    return f;
}

void AnUnreleasedCorpseIsReleased()
{
    Expect("a guild corpse watched past the grace is released", TheHunterAtTheGuards(),
           GuildBodyStep::Release);

    GuildBodyFacts f = TheHunterAtTheGuards();
    f.seconds = GUILD_RELEASE_GRACE_SECONDS;
    Expect("at the grace exactly it is released", f, GuildBodyStep::Release);

    f.seconds = GUILD_RELEASE_GRACE_SECONDS - 1;
    Expect("inside the grace the dead engine and a healer still get their chance", f,
           GuildBodyStep::Wait);

    f = TheHunterAtTheGuards();
    f.clockRunning = false;
    Expect("a first sighting, a new death or a gap starts the clock and waits", f,
           GuildBodyStep::Wait);
}

void AGhostIsNotThisDecisions()
{
    GuildBodyFacts f = TheHunterAtTheGuards();
    f.ghost = true;
    f.health = 1;
    Expect("a released ghost is the ghost recovery's", f, GuildBodyStep::Wait);
}

void ARunKeepsItsOwnDead()
{
    GuildBodyFacts f = TheHunterAtTheGuards();
    f.runTakesBack = true;
    Expect("a corpse a guild run or the raid run-back owns is left to the run", f,
           GuildBodyStep::Wait);
}

void ALivingBodyAtZeroHealthFalls()
{
    GuildBodyFacts f = TheHunterAtTheGuards();
    f.alive = true;
    f.health = 0;
    Expect("a member read alive at zero health past the grace falls", f, GuildBodyStep::Fall);

    f.seconds = GUILD_RELEASE_GRACE_SECONDS - 1;
    Expect("inside the grace a living zero-health reading waits", f, GuildBodyStep::Wait);

    f.seconds = GUILD_RELEASE_GRACE_SECONDS;
    f.health = 1;
    Expect("a living member with health is left alone", f, GuildBodyStep::Wait);

    f.health = 0;
    f.runTakesBack = true;
    Expect("a run's member is the run's even at zero health", f, GuildBodyStep::Wait);
}

void TheGraceIsLongerThanTheFamilys()
{
    // The dead engine releases a corpse on its first few ticks; the family's
    // backstop waits 45 s. The guild's waits longer: it backs up an engine that
    // works for nearly every guild death, not a prompt nobody answers.
    Check("the guild grace is at least two minutes", GUILD_RELEASE_GRACE_SECONDS >= 120);
    Check("and not so long that a stuck corpse waits a quarter hour",
          GUILD_RELEASE_GRACE_SECONDS <= 600);
}

void TheWordsAreTheLogs()
{
    Check("wait word", std::string(GuildBodyStepWord(GuildBodyStep::Wait)) == "wait");
    Check("release word", std::string(GuildBodyStepWord(GuildBodyStep::Release)) == "release");
    Check("fall word", std::string(GuildBodyStepWord(GuildBodyStep::Fall)) == "fall");
}

std::string Read(char const* path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The text of the member function that starts at `signature`, up to the next
// line that closes a member function at the class's indentation.
std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

void TheDriveAsksIt()
{
    std::string const text = Read("src/mod_overseer.cpp");
    Check("src/mod_overseer.cpp is readable (run from the repo root)", !text.empty());

    std::string const drive = Body(text, "void DriveGuildGhostRecovery()");
    Check("DriveGuildGhostRecovery is found", !drive.empty());
    Check("the guild drive asks GuildBodyNext",
          drive.find("OverseerDecisions::GuildBodyNext(") != std::string::npos);
    Check("the release-prompt clock times the unreleased corpse",
          drive.find("OverseerDecisions::ReleasePromptObserve(") != std::string::npos);

    std::size_t const release = drive.find("HandleRepopRequestOpcode(");
    std::size_t const stranded = drive.find("DriveStrandedGhost(bot, botAI, name, now);");
    Check("a release goes through the client's own release handler",
          release != std::string::npos);
    Check("the unreleased corpse is decided before the stranded ghost drive",
          release != std::string::npos && stranded != std::string::npos && release < stranded);
    Check("the release is read back from the world",
          drive.find("released = bot->HasPlayerFlag(PLAYER_FLAGS_GHOST)") != std::string::npos);
    Check("a living body at zero health falls by a death, not a grant",
          drive.find("KillSelf(false)") != std::string::npos);
    // Unit::Kill opens with `if (!victim->GetHealth()) return;` ("prevent
    // killing unit twice"), so KillSelf on a zero-health body is a no-op.
    // Measured after the roll that shipped the fall: the member logged "read
    // alive at zero health" every two minutes and never died. The one point of
    // health is set on the line before the kill and taken by it.
    std::size_t const point = drive.find("bot->SetHealth(1);");
    std::size_t const kill = drive.find("bot->KillSelf(false);");
    Check("the fall gives the body the one point Unit::Kill needs, right before the kill",
          point != std::string::npos && kill != std::string::npos && point < kill &&
              drive.find(';', point) + 1 < kill &&
              drive.substr(point, kill - point).find("if (") == std::string::npos);
    Check("the fall is read back from the world",
          drive.find("fell = !bot->IsAlive()") != std::string::npos);
    Check("the drive never resurrects by hand",
          drive.find("ResurrectPlayer(") == std::string::npos);

    // A death the hook booked restarts the guild clock, as it does the
    // family's, so the hook has to stamp guild deaths too.
    std::string const died = Body(text, "void OnPlayerJustDied(Player* player) override");
    Check("OnPlayerJustDied is found", !died.empty());
    Check("a natural guild member's death is stamped for the release clock",
          died.find("OnRoster(player->GetName()) || InNaturalGuild(player)") != std::string::npos);
}

}  // namespace

int main()
{
    AnUnreleasedCorpseIsReleased();
    AGhostIsNotThisDecisions();
    ARunKeepsItsOwnDead();
    ALivingBodyAtZeroHealthFalls();
    TheGraceIsLongerThanTheFamilys();
    TheWordsAreTheLogs();
    TheDriveAsksIt();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_guild_unreleased: all checks pass\n");
    return 0;
}
