/*
 * A guild run that wipes, or loses its tank or healer, runs back like players.
 *
 * The live failure this pins: a guild run ended the moment everybody inside
 * was dead, and a dead tank or healer nobody raised ended it five minutes
 * later. On the dev realm over 48 hours (2026-10-09), 42 of 105 guild runs
 * ended "wiped" and 19 "abandoned" that way:
 *
 *   run 390, the Deadmines: all five dead 25 minutes in, 1 of 7 bosses down,
 *     ended "everybody inside is dead" at the first wipe.
 *   run 389, Wailing Caverns: only the tank died, the healer alive beside
 *     it, ended "the assigned tank and healer roles were not both alive
 *     inside for 300s".
 *
 * The Deadmines cleared 0 of 46 guild runs in ten days and Wailing Caverns 1
 * of 63: every attempt started again from the door. A group of players
 * releases, runs back from the graveyard, walks in, is raised by the game at
 * the entrance, and carries on with the cleared trash and bosses still dead.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, and reads
 * src/mod_overseer.cpp to pin the wiring (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::GUILD_RUN_CEILING_SECONDS;
using OverseerDecisions::GUILD_RUN_EMPTY_SECONDS;
using OverseerDecisions::GUILD_RUN_RECOVERY_SECONDS;
using OverseerDecisions::GUILD_RUN_REARM_SECONDS;
using OverseerDecisions::GUILD_RUN_ROLE_RECOVERY_SECONDS;
using OverseerDecisions::GuildRunNext;
using OverseerDecisions::GuildRunPoll;
using OverseerDecisions::GuildRunRearmFacts;
using OverseerDecisions::GuildRunRearmNext;
using OverseerDecisions::GuildRunRearmStep;
using OverseerDecisions::GuildRunRecovering;
using OverseerDecisions::GuildRunRecoveryFacts;
using OverseerDecisions::GuildRunRecoveryNext;
using OverseerDecisions::GuildRunRecoveryStep;
using OverseerDecisions::GuildRunVerdict;
using OverseerDecisions::GuildRunVerdictWord;

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

void Expect(char const* what, GuildRunPoll const& poll, GuildRunVerdict want)
{
    GuildRunVerdict const got = GuildRunNext(poll);
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, GuildRunVerdictWord(got),
                GuildRunVerdictWord(want));
    ++failures;
}

// Run 390 at the wipe: five lying dead in the Deadmines, 1 of 7 bosses down.
GuildRunPoll Run390AtTheWipe()
{
    GuildRunPoll poll;
    poll.groupSize = 5;
    poll.inside = 5;
    poll.aliveInside = 0;
    poll.tankAliveInside = false;
    poll.healerAliveInside = false;
    poll.secondsInside = 1506;
    poll.bossesDone = 1;
    poll.bossesTotal = 7;
    return poll;
}

void AWipeIsACorpseRunNotTheEnd()
{
    GuildRunPoll poll = Run390AtTheWipe();
    Expect("run 390 at the wipe goes on to its corpse run", poll, GuildRunVerdict::Running);

    // Released: the five are ghosts at the graveyard outside, nobody inside.
    poll.inside = 0;
    poll.ghostsComingBack = 5;
    poll.secondsRecovering = 60;
    poll.secondsEmpty = GUILD_RUN_EMPTY_SECONDS;
    Expect("five ghosts walking back are not a group that left", poll,
           GuildRunVerdict::Running);

    poll.secondsRecovering = GUILD_RUN_RECOVERY_SECONDS - 1;
    Expect("still running a second before the window ends", poll, GuildRunVerdict::Running);

    poll.secondsRecovering = GUILD_RUN_RECOVERY_SECONDS;
    Expect("nobody back alive inside within the window is a wipe", poll,
           GuildRunVerdict::Wiped);

    // Back: the core raised them at the entrance.
    GuildRunPoll back = Run390AtTheWipe();
    back.aliveInside = 5;
    back.tankAliveInside = true;
    back.healerAliveInside = true;
    Expect("raised inside, the run carries on", back, GuildRunVerdict::Running);
}

void NobodyInsideAndNobodyComingBackIsStillAGroupThatLeft()
{
    GuildRunPoll left;
    left.groupSize = 5;
    left.secondsInside = 600;
    left.secondsEmpty = GUILD_RUN_EMPTY_SECONDS;
    Expect("nobody inside and no ghost coming back is abandoned as before", left,
           GuildRunVerdict::Abandoned);
}

// Run 389: the tank dead, the healer alive beside it.
GuildRunPoll Run389TankDown(unsigned secondsWithoutRoles)
{
    GuildRunPoll poll;
    poll.groupSize = 5;
    poll.inside = 5;
    poll.aliveInside = 4;
    poll.tankAliveInside = false;
    poll.healerAliveInside = true;
    poll.secondsInside = 3000;
    poll.bossesTotal = 7;
    poll.secondsWithoutRoles = secondsWithoutRoles;
    poll.rolesComingBack = true;
    return poll;
}

void ADeadTankIsWaitedForWhileItRunsBack()
{
    Expect("run 389's dead tank at the old five minutes is still coming back",
           Run389TankDown(GUILD_RUN_ROLE_RECOVERY_SECONDS), GuildRunVerdict::Running);
    Expect("and a second before the window ends",
           Run389TankDown(GUILD_RUN_RECOVERY_SECONDS - 1), GuildRunVerdict::Running);
    Expect("a tank not back within the window abandons the run",
           Run389TankDown(GUILD_RUN_RECOVERY_SECONDS), GuildRunVerdict::Abandoned);

    GuildRunPoll elsewhere = Run389TankDown(GUILD_RUN_ROLE_RECOVERY_SECONDS);
    elsewhere.rolesComingBack = false;
    Expect("a tank alive somewhere else is not coming back: five minutes as before", elsewhere,
           GuildRunVerdict::Abandoned);
}

void TheOuterBoundsStillHold()
{
    GuildRunPoll late = Run390AtTheWipe();
    late.secondsInside = GUILD_RUN_CEILING_SECONDS;
    Expect("the ceiling ends a run even on its corpse run", late, GuildRunVerdict::TimedOut);

    GuildRunPoll gone = Run390AtTheWipe();
    gone.groupGone = true;
    Expect("a group that is gone cannot run back", gone, GuildRunVerdict::Abandoned);

    GuildRunPoll last = Run390AtTheWipe();
    last.finderFinished = true;
    Expect("a wipe after the last boss is still a clear", last, GuildRunVerdict::Cleared);
}

void TheDeadAreReleasedWhenNobodyCanRaiseThem()
{
    Check("a wipe with bodies inside is a recovery", GuildRunRecovering(0, 5, 0));
    Check("ghosts coming back are a recovery", GuildRunRecovering(3, 0, 2));
    Check("one dead beside four living is not yet", !GuildRunRecovering(4, 1, 0));
    Check("nobody dead is none", !GuildRunRecovering(5, 0, 0));

    GuildRunRecoveryFacts wipe;
    wipe.deadInside = 5;
    GuildRunRecoveryStep step = GuildRunRecoveryNext(wipe);
    Check("a wipe releases its dead at once", step.release);
    Check("and walks nobody yet", !step.walkBack && !step.holdBrain);

    GuildRunRecoveryFacts tank;
    tank.aliveInside = 4;
    tank.deadInside = 1;
    tank.secondsDeadInside = GUILD_RUN_ROLE_RECOVERY_SECONDS - 1;
    Check("a dead tank beside a living healer is left to be raised",
          !GuildRunRecoveryNext(tank).release);
    tank.secondsDeadInside = GUILD_RUN_ROLE_RECOVERY_SECONDS;
    Check("and released once the rez budget has passed", GuildRunRecoveryNext(tank).release);
}

void TheGhostsAreWalkedBackInsideTheWindow()
{
    GuildRunRecoveryFacts ghosts;
    ghosts.ghostsComingBack = 5;
    ghosts.secondsRecovering = 30;
    GuildRunRecoveryStep step = GuildRunRecoveryNext(ghosts);
    Check("ghosts out are walked back", step.walkBack);
    Check("and the brain waits for them", step.holdBrain);
    Check("nothing is left to release", !step.release);

    ghosts.secondsRecovering = GUILD_RUN_RECOVERY_SECONDS;
    step = GuildRunRecoveryNext(ghosts);
    Check("past the window they are no longer walked", !step.walkBack);
    Check("and the brain is armed for whoever is back", !step.holdBrain);
}

void TheBrainIsArmedForTheWholeGroup()
{
    GuildRunRearmFacts rearm;
    rearm.armed = true;
    rearm.aliveInside = 2;
    rearm.secondsSinceIssued = GUILD_RUN_REARM_SECONDS;
    rearm.regrouping = true;
    Check("two raised of five are not re-armed while three run back",
          GuildRunRearmNext(rearm) == GuildRunRearmStep::Skip);
    rearm.regrouping = false;
    rearm.aliveInside = 5;
    Check("all back, the brain is asked again", GuildRunRearmNext(rearm) == GuildRunRearmStep::Issue);
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

std::string Between(std::string const& source, char const* from, char const* to)
{
    std::size_t const begin = source.find(from);
    std::size_t const end = begin == std::string::npos ? begin : source.find(to, begin);
    if (begin == std::string::npos || end == std::string::npos)
        return std::string();
    return source.substr(begin, end - begin);
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
    std::string const drive = Between(source, "    bool DriveGuildRun(GuildRun& run, std::time_t now)",
                                      "    void DriveGuildRunStrays(");
    Check("DriveGuildRun is found", !drive.empty());
    std::size_t const step = drive.find("bool const regrouping = StepGuildRunRecovery(run, poll, now);");
    std::size_t const verdict = drive.find("OverseerDecisions::GuildRunNext(poll)");
    Check("the corpse run is stepped before the verdict",
          step != std::string::npos && verdict != std::string::npos && step < verdict);
    Check("and holds the re-arm while ghosts come back",
          drive.find("rearm.regrouping = regrouping;") != std::string::npos);

    std::string const recovery = Between(source, "    static bool GuildRunGhostOf(",
                                         "    bool DriveGuildRun(GuildRun& run, std::time_t now)");
    Check("the corpse run's functions are found", !recovery.empty());
    auto has = [&](char const* what, char const* text) {
        Check(what, recovery.find(text) != std::string::npos);
    };
    has("the release is the release button's own repop",
        "p->BuildPlayerRepop();\n            p->RepopAtGraveyard();");
    has("the corpse is read where the core keeps it across maps",
        "GetCorpseLocation().GetMapId() == mapId");
    has("the door is the portal row's entrance trigger",
        "sObjectMgr->GetAreaTrigger(portal->entryTriggerId)");
    has("the knock is the client's packet to the core",
        "StepThroughAreaTrigger(name, ghost, \"trigger:\" + std::to_string(triggerId));");
    has("the walk is routed, never forced",
        "/*generatePath*/ true, /*forceDestination*/ false);");
    has("the decisions are the pure ones", "OverseerDecisions::GuildRunRecoveryNext(facts)");
    has("the deploy proof is said", "overseer: guild run corpse run {}");
    Check("nothing is teleported", recovery.find("TeleportTo") == std::string::npos);
    Check("and nobody is raised by hand", recovery.find("ResurrectPlayer") == std::string::npos);
    Check("the end of a run hands every lease back",
          Between(source, "    void EndGuildRun(", "    static bool LeaveGuildRunQueue(")
                  .find("ReturnGuildRunStayLeases(run, {});") != std::string::npos);
}

}  // namespace

int main()
{
    AWipeIsACorpseRunNotTheEnd();
    NobodyInsideAndNobodyComingBackIsStillAGroupThatLeft();
    ADeadTankIsWaitedForWhileItRunsBack();
    TheOuterBoundsStillHold();
    TheDeadAreReleasedWhenNobodyCanRaiseThem();
    TheGhostsAreWalkedBackInsideTheWindow();
    TheBrainIsArmedForTheWholeGroup();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("guild run recovery: all checks passed\n");
    return 0;
}
