/*
 * A guild group by the dungeon finder: the row, the roles and the verdict.
 *
 * The bot guilds level alone while only the families are directed. The
 * control plane's guild coordinator picks five members by level band and
 * role and sends one `guild` row, `finder-run <keyword> <healer> <dps> <dps>
 * <dps>`, targeted at the tank. These are the three decisions the adapter
 * makes from it: whether the row is this verb and says what it must, which
 * role bits each seat answers the finder's role check with, and when a group
 * that went in is done (cleared, wiped, abandoned, or out of time).
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::FINDER_ROLE_DAMAGE;
using OverseerDecisions::FINDER_ROLE_HEALER;
using OverseerDecisions::FINDER_ROLE_LEADER;
using OverseerDecisions::FINDER_ROLE_TANK;
using OverseerDecisions::GUILD_RUN_CEILING_SECONDS;
using OverseerDecisions::GUILD_RUN_EMPTY_SECONDS;
using OverseerDecisions::GUILD_RUN_RECOVERY_SECONDS;
using OverseerDecisions::GUILD_RUN_ROLE_RECOVERY_SECONDS;
using OverseerDecisions::GuildFinderIsPug;
using OverseerDecisions::GuildFinderKin;
using OverseerDecisions::GuildFinderKinOf;
using OverseerDecisions::GuildFinderKinship;
using OverseerDecisions::GuildFinderRefusal;
using OverseerDecisions::GuildFinderRefusalWord;
using OverseerDecisions::GuildFinderRequest;
using OverseerDecisions::GUILD_RUN_REARM_SECONDS;
using OverseerDecisions::GuildRunNext;
using OverseerDecisions::GuildRunRearmFacts;
using OverseerDecisions::GuildRunRearmNext;
using OverseerDecisions::GuildRunRearmStep;
using OverseerDecisions::GuildRunStrayFacts;
using OverseerDecisions::GuildRunStrayNext;
using OverseerDecisions::GuildRunStrayStep;
using OverseerDecisions::GuildRunPoll;
using OverseerDecisions::GuildRunVerdict;
using OverseerDecisions::GuildRunVerdictWord;
using OverseerDecisions::GuildSeat;
using OverseerDecisions::GuildSeatRoleMask;
using OverseerDecisions::IsGuildFinderRow;
using OverseerDecisions::ParseGuildFinderRequest;

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

void TheRowIsRoutedOnItsFirstWord()
{
    Check("a finder-run row is this verb",
          IsGuildFinderRow("finder-run deadmines Ann Bo Cy Di"));
    Check("leading spaces do not hide it", IsGuildFinderRow("  finder-run deadmines A B C D"));
    Check("DoGuild's own verbs are not", !IsGuildFinderRow("view"));
    Check("nor is a guild name that starts the same",
          !IsGuildFinderRow("finder-runs deadmines A B C D"));
    Check("an empty row is not", !IsGuildFinderRow(""));
}

void AWellFormedRowNamesTheDoorAndTheFourOthers()
{
    GuildFinderRequest const r =
        ParseGuildFinderRequest("finder-run ragefire Priestly Mage Rogue Lock", "Warrior");
    Check("parses", r.error == GuildFinderRefusal::None);
    Check("the keyword", r.keyword == "ragefire");
    Check("the healer", r.healer == "Priestly");
    Check("three damage dealers", r.damage.size() == 3);
    Check("in order", r.damage.size() == 3 && r.damage[0] == "Mage" && r.damage[2] == "Lock");

    GuildFinderRequest const hyphen =
        ParseGuildFinderRequest("finder-run scarlet-library A B C D", "Tank");
    Check("a hyphenated wing parses", hyphen.error == GuildFinderRefusal::None &&
                                          hyphen.keyword == "scarlet-library");
}

void AMalformedRowIsRefusedWithItsGrammar()
{
    Check("four names, not three",
          ParseGuildFinderRequest("finder-run deadmines A B C", "T").error ==
              GuildFinderRefusal::Malformed);
    Check("not five either",
          ParseGuildFinderRequest("finder-run deadmines A B C D E", "T").error ==
              GuildFinderRefusal::Malformed);
    Check("a keyword with a digit is not a portal keyword",
          ParseGuildFinderRequest("finder-run dead2mines A B C D", "T").error ==
              GuildFinderRefusal::Malformed);
    Check("an upper-case keyword is not either",
          ParseGuildFinderRequest("finder-run Deadmines A B C D", "T").error ==
              GuildFinderRefusal::Malformed);
    Check("the grammar is said",
          std::string(GuildFinderRefusalWord(GuildFinderRefusal::Malformed)).find(
              "finder-run <keyword>") != std::string::npos);
    Check("another verb is not this one",
          ParseGuildFinderRequest("view", "T").error == GuildFinderRefusal::NotThisVerb);
}

void NamesAreCharacterNamesAndNeverTwice()
{
    Check("a digit in a name",
          ParseGuildFinderRequest("finder-run deadmines A B C D4", "T").error ==
              GuildFinderRefusal::BadName);
    Check("a quote in a name, which could reach SQL",
          ParseGuildFinderRequest("finder-run deadmines A B C D'x", "T").error ==
              GuildFinderRefusal::BadName);
    Check("a name longer than the core allows",
          ParseGuildFinderRequest("finder-run deadmines A B C Abcdefghijklm", "T").error ==
              GuildFinderRefusal::BadName);
    Check("the tank named again as a damage dealer",
          ParseGuildFinderRequest("finder-run deadmines A B C Tank", "Tank").error ==
              GuildFinderRefusal::SameNameTwice);
    Check("case does not make two characters of one",
          ParseGuildFinderRequest("finder-run deadmines Ann B ann D", "T").error ==
              GuildFinderRefusal::SameNameTwice);
    Check("a tank that is not a name",
          ParseGuildFinderRequest("finder-run deadmines A B C D", "").error ==
              GuildFinderRefusal::BadName);
}

void EachSeatAnswersWithItsOwnRoleAndTheTankLeads()
{
    Check("the tank tanks and leads",
          GuildSeatRoleMask(GuildSeat::Tank) == (FINDER_ROLE_LEADER | FINDER_ROLE_TANK));
    Check("the healer heals and nothing else",
          GuildSeatRoleMask(GuildSeat::Healer) == FINDER_ROLE_HEALER);
    Check("a damage dealer deals damage and nothing else",
          GuildSeatRoleMask(GuildSeat::Damage) == FINDER_ROLE_DAMAGE);
}

GuildRunPoll Inside(unsigned inside, unsigned alive)
{
    GuildRunPoll poll;
    poll.groupSize = 5;
    poll.inside = inside;
    poll.aliveInside = alive;
    poll.secondsInside = 600;
    poll.bossesTotal = 4;
    return poll;
}

void ARunningGroupIsLeftToRun()
{
    Check("five inside, all alive, two bosses down",
          [] {
              GuildRunPoll p = Inside(5, 5);
              p.bossesDone = 2;
              return GuildRunNext(p) == GuildRunVerdict::Running;
          }());
    Check("one dead is not a wipe", GuildRunNext(Inside(5, 4)) == GuildRunVerdict::Running);
    Check("nobody inside for a moment is not yet gone",
          [] {
              GuildRunPoll p = Inside(0, 0);
              p.secondsEmpty = GUILD_RUN_EMPTY_SECONDS - 1;
              return GuildRunNext(p) == GuildRunVerdict::Running;
          }());
}

void TheFinderSayingFinishedIsACleared()
{
    GuildRunPoll p = Inside(5, 5);
    p.finderFinished = true;
    Check("the core's finished dungeon clears it", GuildRunNext(p) == GuildRunVerdict::Cleared);
    GuildRunPoll bosses = Inside(5, 5);
    bosses.bossesDone = 4;
    Check("every boss down clears it", GuildRunNext(bosses) == GuildRunVerdict::Cleared);
    GuildRunPoll after = Inside(5, 0);
    after.finderFinished = true;
    Check("a wipe after the last boss is still a clear", GuildRunNext(after) ==
                                                            GuildRunVerdict::Cleared);
    GuildRunPoll none = Inside(5, 5);
    none.bossesTotal = 0;
    Check("an instance with no boss list is not cleared by it",
          GuildRunNext(none) == GuildRunVerdict::Running);
}

void AScriptlessInstanceIsClearedByTheCoreCreditMask()
{
    // Ragefire Chasm's script never calls SetBossState, so the run reads
    // bossesTotal 0 and only the finder flag could clear it. The core's own
    // encounter credit is the other witness.
    GuildRunPoll p = Inside(5, 5);
    p.bossesTotal = 0;
    p.expectedMask = 0b1111;
    p.creditedMask = 0b1111;
    Check("every credited encounter clears a run with no boss list",
          GuildRunNext(p) == GuildRunVerdict::Cleared);
    p.creditedMask = 0b0111;
    Check("three of four credited is still running", GuildRunNext(p) == GuildRunVerdict::Running);
    p.expectedMask = 0;
    p.creditedMask = 0b1111;
    Check("a map that credits nothing cannot be cleared by the mask",
          GuildRunNext(p) == GuildRunVerdict::Running);
}

void EverybodyInsideDeadIsAWipe()
{
    // A wipe is a corpse run first (GUILD_RUN_RECOVERY_SECONDS, and
    // tests/test_guild_run_recovery.cpp): it is Wiped once nobody is back
    // alive inside within the window.
    GuildRunPoll spent = Inside(3, 0);
    spent.secondsRecovering = GUILD_RUN_RECOVERY_SECONDS;
    Check("three inside, none alive, the window spent", GuildRunNext(spent) == GuildRunVerdict::Wiped);
    Check("three inside, none alive, just now, is a corpse run",
          GuildRunNext(Inside(3, 0)) == GuildRunVerdict::Running);
    Check("nobody inside is not a wipe",
          GuildRunNext(Inside(0, 0)) != GuildRunVerdict::Wiped);
}

void ARunWithoutItsTankOrHealerGetsAbandonedAfterRecoveryGrace()
{
    GuildRunPoll missingBoth = Inside(3, 3);
    missingBoth.tankAliveInside = false;
    missingBoth.healerAliveInside = false;
    missingBoth.secondsWithoutRoles = GUILD_RUN_ROLE_RECOVERY_SECONDS;
    Check("three living damage dealers cannot keep a run without tank and healer alive",
          GuildRunNext(missingBoth) == GuildRunVerdict::Abandoned);

    GuildRunPoll missingTank = missingBoth;
    missingTank.healerAliveInside = true;
    Check("a run also yields when only its assigned tank is missing",
          GuildRunNext(missingTank) == GuildRunVerdict::Abandoned);

    GuildRunPoll missingHealer = missingBoth;
    missingHealer.tankAliveInside = true;
    Check("a run also yields when only its assigned healer is missing",
          GuildRunNext(missingHealer) == GuildRunVerdict::Abandoned);

    GuildRunPoll withinGrace = missingBoth;
    withinGrace.secondsWithoutRoles = GUILD_RUN_ROLE_RECOVERY_SECONDS - 1;
    Check("a brief role absence gets time to recover",
          GuildRunNext(withinGrace) == GuildRunVerdict::Running);

    GuildRunPoll rolesReturned = missingBoth;
    rolesReturned.tankAliveInside = true;
    rolesReturned.healerAliveInside = true;
    Check("the run continues when both assigned roles return",
          GuildRunNext(rolesReturned) == GuildRunVerdict::Running);
}

void AGroupThatLeftOrOutstayedIsOver()
{
    GuildRunPoll empty = Inside(0, 0);
    empty.secondsEmpty = GUILD_RUN_EMPTY_SECONDS;
    Check("nobody inside past the grace is abandoned",
          GuildRunNext(empty) == GuildRunVerdict::Abandoned);
    GuildRunPoll gone = Inside(5, 5);
    gone.groupGone = true;
    Check("a group that is gone is abandoned", GuildRunNext(gone) == GuildRunVerdict::Abandoned);
    GuildRunPoll late = Inside(5, 5);
    late.secondsInside = GUILD_RUN_CEILING_SECONDS;
    Check("inside past the ceiling is timed out", GuildRunNext(late) == GuildRunVerdict::TimedOut);
    Check("the words", std::string(GuildRunVerdictWord(GuildRunVerdict::TimedOut)) == "timed out" &&
                           std::string(GuildRunVerdictWord(GuildRunVerdict::Cleared)) == "cleared");
}

// A guild run sat 4000s at 0 of 4 bosses after the dungeon brain disabled itself
// on a healer death, because the group was armed once per stay and nothing ever
// asked again. The re-arm is an idempotent verb, so what is decided here is only
// WHEN to ask: after the first arming, while somebody is alive, once per cooldown.
void ADisabledBrainIsAskedAgainWhileSomebodyLives()
{
    GuildRunRearmFacts f;
    f.armed = true;
    f.aliveInside = 4;
    f.secondsSinceIssued = GUILD_RUN_REARM_SECONDS;
    Check("armed, alive, cooldown spent: ask again",
          GuildRunRearmNext(f) == GuildRunRearmStep::Issue);
    f.aliveInside = 1;
    Check("one survivor is enough", GuildRunRearmNext(f) == GuildRunRearmStep::Issue);
}

void TheReArmWaitsOutItsCooldownAndNeverPrecedesTheFirstArming()
{
    GuildRunRearmFacts f;
    f.armed = true;
    f.aliveInside = 4;
    f.secondsSinceIssued = GUILD_RUN_REARM_SECONDS - 1;
    Check("inside the cooldown: wait", GuildRunRearmNext(f) == GuildRunRearmStep::Wait);

    f.secondsSinceIssued = GUILD_RUN_REARM_SECONDS * 10;
    f.armed = false;
    Check("not armed yet: the first arming owns it", GuildRunRearmNext(f) == GuildRunRearmStep::Skip);

    f.armed = true;
    f.aliveInside = 0;
    Check("nobody alive: nothing to re-arm", GuildRunRearmNext(f) == GuildRunRearmStep::Skip);
}

// The live trace (2026-10-04): a restart left 'Velalenn' a stray, and eleven
// minutes later Velalenn was seated in a NEW run under another tank. Once that
// group was in the dungeon it was a finder group, and the stray took the new
// run's members out and disbanded the group: "abandoned: the group is gone".
void ARestartsStrayNeverTakesANewRunsGroup()
{
    GuildRunStrayFacts f;
    f.tankInGroup = true;
    f.finderGroup = true;
    f.liveRunGroup = true;
    Check("the stray's tank in a live run's finder group: the stray is dropped",
          GuildRunStrayNext(f) == GuildRunStrayStep::Drop);
    f.takenOut = true;
    Check("even after an earlier take-out, a live run's group is never disbanded",
          GuildRunStrayNext(f) == GuildRunStrayStep::Drop);
    f.finderGroup = false;
    f.takenOut = false;
    Check("a live run still queueing (no finder flag yet) is not the stray's either",
          GuildRunStrayNext(f) == GuildRunStrayStep::Drop);
}

void ARestartsOwnLeftoverIsStillTakenOutThenDisbanded()
{
    GuildRunStrayFacts f;
    Check("the tank not back yet: wait", GuildRunStrayNext(f) == GuildRunStrayStep::Wait);
    f.expired = true;
    Check("the quarter hour over with no group: forget it",
          GuildRunStrayNext(f) == GuildRunStrayStep::Drop);

    f.expired = false;
    f.tankInGroup = true;
    Check("a plain party is not the finder group: wait",
          GuildRunStrayNext(f) == GuildRunStrayStep::Wait);
    f.finderGroup = true;
    Check("the leftover finder group: take the living out first",
          GuildRunStrayNext(f) == GuildRunStrayStep::TakeOut);
    f.takenOut = true;
    Check("then disband it", GuildRunStrayNext(f) == GuildRunStrayStep::Disband);
}

}  // namespace

// A PICK-UP GROUP (wow-overseer issue 591): the row names one of the five as
// a pug, and only that member may come from outside the guild.
void APugIsOneOfTheFiveNamedAfterThem()
{
    GuildFinderRequest const r = ParseGuildFinderRequest(
        "finder-run deadmines Thrall Mage Rogue Lock pug Thrall", "Warrior");
    Check("a row with a pug parses", r.error == GuildFinderRefusal::None);
    Check("the five are as before",
          r.healer == "Thrall" && r.damage.size() == 3 && r.damage[2] == "Lock");
    Check("the pug is named", r.pugs.size() == 1 && r.pugs[0] == "Thrall");
    Check("the pug is found without case", GuildFinderIsPug(r, "thrall"));
    Check("a guildmate is not a pug", !GuildFinderIsPug(r, "Mage"));

    GuildFinderRequest const tank = ParseGuildFinderRequest(
        "finder-run deadmines Priest Mage Rogue Lock pug Warrior", "Warrior");
    Check("the tank may be the pug",
          tank.error == GuildFinderRefusal::None && GuildFinderIsPug(tank, "Warrior"));

    GuildFinderRequest const both = ParseGuildFinderRequest(
        "finder-run deadmines Thrall Mage Rogue Lock pug Warrior pug Thrall", "Warrior");
    Check("a tank and a healer may both be pugs",
          both.error == GuildFinderRefusal::None && both.pugs.size() == 2);

    GuildFinderRequest const none =
        ParseGuildFinderRequest("finder-run deadmines A B C D", "T");
    Check("a row of guildmates names no pug", none.error == GuildFinderRefusal::None &&
                                                  none.pugs.empty() && !GuildFinderIsPug(none, "A"));
}

void APugTheRowCannotPlaceIsRefused()
{
    Check("a pug who is not one of the five",
          ParseGuildFinderRequest("finder-run deadmines A B C D pug Stranger", "T").error ==
              GuildFinderRefusal::PugNotInGroup);
    Check("the pug word with no name",
          ParseGuildFinderRequest("finder-run deadmines A B C D pug", "T").error ==
              GuildFinderRefusal::Malformed);
    Check("another word in the pug's place",
          ParseGuildFinderRequest("finder-run deadmines A B C D friend A", "T").error ==
              GuildFinderRefusal::Malformed);
    Check("one pug named twice",
          ParseGuildFinderRequest("finder-run deadmines A B C D pug A pug A", "T").error ==
              GuildFinderRefusal::SameNameTwice);
    Check("no more than two pugs",
          ParseGuildFinderRequest("finder-run deadmines A B C D pug A pug B pug C", "T").error ==
              GuildFinderRefusal::TooManyPugs);
    Check("a pug's name is a character name",
          ParseGuildFinderRequest("finder-run deadmines A B C D pug A1", "T").error ==
              GuildFinderRefusal::BadName);
    Check("the refusal says so",
          std::string(GuildFinderRefusalWord(GuildFinderRefusal::PugNotInGroup)).find("pug") !=
              std::string::npos);
}

void OnlyAPugMayComeFromOutsideTheGuildAndNeverFromTheOtherSide()
{
    GuildFinderKin mate;
    mate.guildId = 7;
    mate.team = 0;
    mate.anchorGuildId = 7;
    mate.anchorTeam = 0;
    Check("a guildmate is kin", GuildFinderKinOf(mate) == GuildFinderKinship::Kin);

    GuildFinderKin stranger = mate;
    stranger.guildId = 0;
    Check("a guildless member who is not a pug is refused",
          GuildFinderKinOf(stranger) == GuildFinderKinship::NotInGuild);

    GuildFinderKin pug = stranger;
    pug.pug = true;
    Check("a guildless pug of the same side is kin", GuildFinderKinOf(pug) == GuildFinderKinship::Kin);
    pug.guildId = 9;
    Check("a pug from another guild is kin too", GuildFinderKinOf(pug) == GuildFinderKinship::Kin);
    pug.team = 1;
    Check("a pug of the other side is refused",
          GuildFinderKinOf(pug) == GuildFinderKinship::OtherSide);

    GuildFinderKin lone = mate;
    lone.anchorGuildId = 0;
    Check("no guild group without a guild",
          GuildFinderKinOf(lone) == GuildFinderKinship::AnchorInNoGuild);
}

int main()
{
    TheRowIsRoutedOnItsFirstWord();
    AWellFormedRowNamesTheDoorAndTheFourOthers();
    AMalformedRowIsRefusedWithItsGrammar();
    NamesAreCharacterNamesAndNeverTwice();
    EachSeatAnswersWithItsOwnRoleAndTheTankLeads();
    ARunningGroupIsLeftToRun();
    TheFinderSayingFinishedIsACleared();
    AScriptlessInstanceIsClearedByTheCoreCreditMask();
    EverybodyInsideDeadIsAWipe();
    ARunWithoutItsTankOrHealerGetsAbandonedAfterRecoveryGrace();
    AGroupThatLeftOrOutstayedIsOver();
    ADisabledBrainIsAskedAgainWhileSomebodyLives();
    TheReArmWaitsOutItsCooldownAndNeverPrecedesTheFirstArming();
    ARestartsStrayNeverTakesANewRunsGroup();
    ARestartsOwnLeftoverIsStillTakenOutThenDisbanded();
    APugIsOneOfTheFiveNamedAfterThem();
    APugTheRowCannotPlaceIsRefused();
    OnlyAPugMayComeFromOutsideTheGuildAndNeverFromTheOtherSide();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_guild_finder_run: ok\n");
    return 0;
}
