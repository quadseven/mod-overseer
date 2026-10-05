/*
 * A retire deletes one factory-made random bot through the core's own
 * Player::DeleteFromDB (2026-10-05, the operator retires the factory bots).
 * These pin who may be retired, how, and the line that records it.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <string>

using OverseerDecisions::AccountNameHasPrefix;
using OverseerDecisions::IsRetireRow;
using OverseerDecisions::ParseRetireRequest;
using OverseerDecisions::RETIRE_CEILING_MS;
using OverseerDecisions::RETIRE_MIN_LEVEL;
using OverseerDecisions::RETIRE_SETTLE_MS;
using OverseerDecisions::RetireFacts;
using OverseerDecisions::RetireLogLine;
using OverseerDecisions::RetirePlan;
using OverseerDecisions::RetirePlanFor;
using OverseerDecisions::RetireRefusal;
using OverseerDecisions::RetireRefusalPasses;
using OverseerDecisions::RetireRefusalSaid;
using OverseerDecisions::RetireRequest;
using OverseerDecisions::RetireVerdictFor;
using OverseerDecisions::RetireWait;
using OverseerDecisions::RetireWaitFor;

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

void TheRow()
{
    Check("retire is a retire row", IsRetireRow("retire"));
    Check("padded, still a retire row", IsRetireRow("  retire "));
    Check("a job mode is not", !IsRetireRow("quest"));
    Check("a rename is not", !IsRetireRow("rename-to Bonk"));
    Check("a word that only starts the same is not", !IsRetireRow("retired"));
    Check("the one word parses", ParseRetireRequest(" retire ").ok);
    Check("anything after it is refused", !ParseRetireRequest("retire Bonk").ok);
    Check("an empty row is refused", !ParseRetireRequest("").ok);
}

// A level 60 factory mage, offline, in a playerbots guild: retired.
RetireFacts Factory()
{
    RetireFacts f;
    f.exists = true;
    f.randomBotAccount = true;
    f.level = 60;
    return f;
}

RetireRequest Ok()
{
    return ParseRetireRequest("retire");
}

void WhoMayBeRetired()
{
    Check("a factory bot at 60 is retired", RetireVerdictFor(Ok(), Factory()) == RetireRefusal::None);

    RetireFacts f = Factory();
    f.level = RETIRE_MIN_LEVEL;
    Check("level 42 is retired", RetireVerdictFor(Ok(), f) == RetireRefusal::None);
    f.level = RETIRE_MIN_LEVEL - 1;
    Check("level 41, the realm's natural top, is not",
          RetireVerdictFor(Ok(), f) == RetireRefusal::BelowLevel);
    Check("the line is 42", RETIRE_MIN_LEVEL == 42);

    f = Factory();
    f.randomBotAccount = false;
    Check("not off a real player's account",
          RetireVerdictFor(Ok(), f) == RetireRefusal::NotRandomBotAccount);

    f = Factory();
    f.keptGuild = true;
    Check("not out of Cave or Bonkers", RetireVerdictFor(Ok(), f) == RetireRefusal::KeptGuild);

    f = Factory();
    f.onRoster = true;
    Check("not off the roster", RetireVerdictFor(Ok(), f) == RetireRefusal::OnRoster);

    f = Factory();
    f.deathKnight = true;
    Check("not a factory death knight", RetireVerdictFor(Ok(), f) == RetireRefusal::DeathKnight);

    f = Factory();
    f.exists = false;
    Check("not a name nobody has", RetireVerdictFor(Ok(), f) == RetireRefusal::NoSuchCharacter);

    Check("not on a malformed row",
          RetireVerdictFor(ParseRetireRequest("retire now"), Factory()) == RetireRefusal::BadRequest);
}

void WhereItIs()
{
    RetireFacts f = Factory();
    f.betweenWorlds = true;
    Check("not while logging in or out", RetireVerdictFor(Ok(), f) == RetireRefusal::BetweenWorlds);

    f = Factory();
    f.clientAttached = true;
    Check("not under a real client", RetireVerdictFor(Ok(), f) == RetireRefusal::ClientAttached);

    // WHO it is outranks WHERE it is: a kept character between worlds is
    // refused as kept, a reason no retry clears.
    f = Factory();
    f.keptGuild = true;
    f.betweenWorlds = true;
    Check("who outranks where", RetireVerdictFor(Ok(), f) == RetireRefusal::KeptGuild);

    Check("between worlds passes", RetireRefusalPasses(RetireRefusal::BetweenWorlds));
    Check("a client passes", RetireRefusalPasses(RetireRefusal::ClientAttached));
    Check("a kept guild does not", !RetireRefusalPasses(RetireRefusal::KeptGuild));
    Check("the roster does not", !RetireRefusalPasses(RetireRefusal::OnRoster));
    Check("a death knight does not", !RetireRefusalPasses(RetireRefusal::DeathKnight));
    Check("a low level does not", !RetireRefusalPasses(RetireRefusal::BelowLevel));
}

void EveryRefusalIsSaid()
{
    for (RetireRefusal r : {RetireRefusal::BadRequest, RetireRefusal::NoSuchCharacter,
                            RetireRefusal::NotRandomBotAccount, RetireRefusal::KeptGuild,
                            RetireRefusal::OnRoster, RetireRefusal::DeathKnight, RetireRefusal::BelowLevel,
                            RetireRefusal::BetweenWorlds, RetireRefusal::ClientAttached})
        Check("every refusal has words", std::strlen(RetireRefusalSaid(r)) > 0);
    Check("none has none", std::strlen(RetireRefusalSaid(RetireRefusal::None)) == 0);
}

void HowItIsCarriedOut()
{
    Check("refused is refused", RetirePlanFor(RetireRefusal::OnRoster, false) == RetirePlan::Refuse);
    Check("refused in the world is refused", RetirePlanFor(RetireRefusal::DeathKnight, true) == RetirePlan::Refuse);
    Check("offline settles", RetirePlanFor(RetireRefusal::None, false) == RetirePlan::Settle);
    Check("a headless bot in the world is logged out first",
          RetirePlanFor(RetireRefusal::None, true) == RetirePlan::EvictThenSettle);
}

void TheWait()
{
    Check("just out: wait", RetireWaitFor(false, 0, 0) == RetireWait::Wait);
    Check("not yet settled: wait", RetireWaitFor(false, RETIRE_SETTLE_MS - 1, RETIRE_SETTLE_MS - 1) == RetireWait::Wait);
    Check("settled: delete", RetireWaitFor(false, RETIRE_SETTLE_MS, RETIRE_SETTLE_MS) == RetireWait::Delete);
    Check("back in the world: log it out again", RetireWaitFor(true, 0, 30000) == RetireWait::Evict);
    Check("never stays out: give up", RetireWaitFor(true, 0, RETIRE_CEILING_MS) == RetireWait::GiveUp);
    Check("the ceiling outranks a settle", RetireWaitFor(false, RETIRE_CEILING_MS, RETIRE_CEILING_MS) == RetireWait::GiveUp);
    Check("the settle is shorter than the ceiling", RETIRE_SETTLE_MS < RETIRE_CEILING_MS);
}

void TheAccount()
{
    Check("an upper-case bot account matches a lower-case prefix", AccountNameHasPrefix("RNDBOT412", "rndbot"));
    Check("the same case matches", AccountNameHasPrefix("rndbot0", "rndbot"));
    Check("a player's account does not", !AccountNameHasPrefix("PLAYERONE", "rndbot"));
    Check("the prefix must lead", !AccountNameHasPrefix("XRNDBOT1", "rndbot"));
    Check("shorter than the prefix does not", !AccountNameHasPrefix("RND", "rndbot"));
    Check("an empty prefix matches nobody", !AccountNameHasPrefix("RNDBOT1", ""));
}

void TheLine()
{
    std::string const line = RetireLogLine("Abcbot", 60, 8, "Carpe Diem");
    Check("names the character", line.find("'Abcbot'") != std::string::npos);
    Check("names the level and class", line.find("level 60 Mage") != std::string::npos);
    Check("names the guild", line.find("guild 'Carpe Diem'") != std::string::npos);
    Check("names the core's delete", line.find("Player::DeleteFromDB") != std::string::npos);
    Check("a guildless bot says so", RetireLogLine("Abc", 57, 1, "").find("no guild") != std::string::npos);
    Check("an unknown class keeps its id", RetireLogLine("Abc", 57, 99, "").find("class 99") != std::string::npos);
}

}  // namespace

int main()
{
    TheRow();
    WhoMayBeRetired();
    WhereItIs();
    EveryRefusalIsSaid();
    HowItIsCarriedOut();
    TheWait();
    TheAccount();
    TheLine();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
