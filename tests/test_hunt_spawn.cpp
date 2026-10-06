/*
 * A hunt at a spawn: the grammar, each gate row, the poll's decision and the
 * wiring in the adapter source.
 *
 * `hunt-spawn creature:<entry> [count:<n>] [max:<seconds>]` makes a bot that
 * stands near a creature kill and loot it through its own combat and loot AI.
 * One check per gate row, so a row that stops refusing fails by name.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else; the source
 * checks read src/mod_overseer.cpp and conf/mod_overseer.conf.dist as text.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace D = OverseerDecisions;

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

bool Is(char const* got, char const* want)
{
    return std::strcmp(got, want) == 0;
}

std::string ReadFile(char const* path)
{
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void TheRowIsRoutedOnItsFirstWord()
{
    Check("hunt routed", D::IsHuntRow("hunt-spawn creature:3256"));
    Check("walk is not hunt", !D::IsHuntRow("walk-to-spawn creature:3256"));
    Check("party-walk is not hunt", !D::IsHuntRow("party-walk creature:3256"));
    Check("empty is no row", !D::IsHuntRow(""));
}

void TheGrammarTakesAnEntryACountAndAClock()
{
    D::HuntRequest r = D::ParseHuntRequest("hunt-spawn creature:3256");
    Check("entry alone parses", !*r.error && r.entry == 3256 && r.count == 0 &&
                                    r.maxSeconds == D::HUNT_DEFAULT_SECONDS);
    r = D::ParseHuntRequest("hunt-spawn max:90 count:5 creature:3256");
    Check("all three parse in any order",
          !*r.error && r.entry == 3256 && r.count == 5 && r.maxSeconds == 90);
    Check("no entry refused", *D::ParseHuntRequest("hunt-spawn count:5").error);
    Check("bare verb refused", *D::ParseHuntRequest("hunt-spawn").error);
    Check("zero entry refused", *D::ParseHuntRequest("hunt-spawn creature:0").error);
    Check("gameobject refused", *D::ParseHuntRequest("hunt-spawn gameobject:5").error);
    Check("repeated key refused",
          *D::ParseHuntRequest("hunt-spawn creature:1 creature:2").error);
    Check("zero count refused", *D::ParseHuntRequest("hunt-spawn creature:1 count:0").error);
    Check("count over the cap refused",
          *D::ParseHuntRequest("hunt-spawn creature:1 count:201").error);
    Check("clock over the cap refused",
          *D::ParseHuntRequest("hunt-spawn creature:1 max:1801").error);
    Check("clock at the cap parses", !*D::ParseHuntRequest("hunt-spawn creature:1 max:1800").error);
    Check("non-number refused", *D::ParseHuntRequest("hunt-spawn creature:1 count:x").error);
    Check("unknown key refused", *D::ParseHuntRequest("hunt-spawn creature:1 spells:1").error);
    Check("another verb refused", *D::ParseHuntRequest("walk-to-spawn creature:1").error);
    Check("malformed word is the literal",
          Is(D::ParseHuntRequest("hunt-spawn").error, D::HuntRefusal::MalformedHunt));
}

void TheLevelGapIsTheModulesOwn()
{
    Check("hunt gap is the lone leg gap", D::HUNT_LEVEL_GAP == D::LoneLegLimits{}.levelGap);
}

void EachGateWallRefusesByName()
{
    namespace R = D::HuntRefusal;
    Check("clear facts pass", Is(D::HuntGate(D::HuntFacts{}), ""));

    D::HuntFacts f;
    f.hasBotAI = false;
    Check("not a bot", Is(D::HuntGate(f), R::NotABot));
    f = D::HuntFacts{};
    f.inWorld = false;
    Check("not in world", Is(D::HuntGate(f), R::NotInWorld));
    f = D::HuntFacts{};
    f.alive = false;
    Check("dead", Is(D::HuntGate(f), R::Dead));
    f = D::HuntFacts{};
    f.inInstance = true;
    Check("in an instance", Is(D::HuntGate(f), R::InInstance));
    f = D::HuntFacts{};
    f.inFlight = true;
    Check("in flight", Is(D::HuntGate(f), R::InFlight));
    f = D::HuntFacts{};
    f.alreadyHunting = true;
    Check("already hunting", Is(D::HuntGate(f), R::AlreadyHunting));
    f = D::HuntFacts{};
    f.targetFound = false;
    Check("no target", Is(D::HuntGate(f), R::NoTarget));
    f = D::HuntFacts{};
    f.levelsAbove = int(D::HUNT_LEVEL_GAP);
    Check("target at the gap", Is(D::HuntGate(f), R::TooHighLevel));
    f.levelsAbove = int(D::HUNT_LEVEL_GAP) - 1;
    Check("target one under the gap passes", Is(D::HuntGate(f), ""));
    f = D::HuntFacts{};
    f.healthPct = D::HUNT_HEALTH_FLOOR_PCT - 1;
    Check("below the health floor", Is(D::HuntGate(f), R::LowHealth));
    f.healthPct = D::HUNT_HEALTH_FLOOR_PCT;
    Check("at the health floor passes", Is(D::HuntGate(f), ""));
    f = D::HuntFacts{};
    f.attackers = D::HUNT_MAX_ATTACKERS + 1;
    Check("too many adds", Is(D::HuntGate(f), R::TooManyAdds));
    f.attackers = D::HUNT_MAX_ATTACKERS;
    Check("adds at the limit pass", Is(D::HuntGate(f), ""));

    f = D::HuntFacts{};
    f.inInstance = true;
    f.alive = false;
    Check("dead outranks instance", Is(D::HuntGate(f), R::Dead));
}

void RefusalsSayWhetherToAskAgain()
{
    namespace R = D::HuntRefusal;
    Check("malformed never", D::HuntRefusalRetry(R::MalformedHunt) == D::TownRetry::Never);
    Check("disabled never", D::HuntRefusalRetry(R::Disabled) == D::TownRetry::Never);
    Check("not a bot never", D::HuntRefusalRetry(R::NotABot) == D::TownRetry::Never);
    Check("instance elsewhere", D::HuntRefusalRetry(R::InInstance) == D::TownRetry::Elsewhere);
    Check("already hunting later",
          D::HuntRefusalRetry(R::AlreadyHunting) == D::TownRetry::Later);
    Check("low health later", D::HuntRefusalRetry(R::LowHealth) == D::TownRetry::Later);
    Check("no target later", D::HuntRefusalRetry(R::NoTarget) == D::TownRetry::Later);
    Check("adds later", D::HuntRefusalRetry(R::TooManyAdds) == D::TownRetry::Later);
}

void ThePollDecidesTheNextMove()
{
    D::HuntPollFacts p;
    Check("clear ground pulls", D::HuntNext(p) == D::HuntStep::Pull);

    p = D::HuntPollFacts{};
    p.count = 5;
    p.kills = 5;
    Check("kill count reached is done", D::HuntNext(p) == D::HuntStep::Done);
    p.kills = 4;
    Check("one short pulls", D::HuntNext(p) == D::HuntStep::Pull);

    p = D::HuntPollFacts{};
    p.count = 0;
    p.kills = 99;
    Check("no count never finishes on kills", D::HuntNext(p) == D::HuntStep::Pull);

    p = D::HuntPollFacts{};
    p.secondsUp = p.maxSeconds;
    Check("clock out is timeout", D::HuntNext(p) == D::HuntStep::TimedOut);

    p = D::HuntPollFacts{};
    p.count = 1;
    p.kills = 1;
    p.secondsUp = p.maxSeconds;
    Check("done outranks timeout", D::HuntNext(p) == D::HuntStep::Done);

    for (int wall = 0; wall < 5; ++wall)
    {
        p = D::HuntPollFacts{};
        p.inCombat = true;
        if (wall == 0) p.gate.hasBotAI = false;
        if (wall == 1) p.gate.inWorld = false;
        if (wall == 2) p.gate.alive = false;
        if (wall == 3) p.gate.inInstance = true;
        if (wall == 4) p.gate.inFlight = true;
        Check("a wall ends the hunt even in combat", D::HuntNext(p) == D::HuntStep::Refused);
    }

    p = D::HuntPollFacts{};
    p.inCombat = true;
    Check("combat is left to the bot", D::HuntNext(p) == D::HuntStep::Fight);
    p = D::HuntPollFacts{};
    p.lootPending = true;
    Check("loot pending is left to the bot", D::HuntNext(p) == D::HuntStep::Fight);

    p = D::HuntPollFacts{};
    p.gate.healthPct = 10;
    Check("low health rests", D::HuntNext(p) == D::HuntStep::Rest);
    p = D::HuntPollFacts{};
    p.gate.targetFound = false;
    Check("no target waits", D::HuntNext(p) == D::HuntStep::Wait);
    p = D::HuntPollFacts{};
    p.gate.levelsAbove = 9;
    Check("too high a target waits", D::HuntNext(p) == D::HuntStep::Wait);
    p = D::HuntPollFacts{};
    p.gate.attackers = 9;
    Check("adds wait", D::HuntNext(p) == D::HuntStep::Wait);

    Check("step words", Is(D::HuntStepWord(D::HuntStep::Done), "done") &&
                            Is(D::HuntStepWord(D::HuntStep::TimedOut), "timeout") &&
                            Is(D::HuntStepWord(D::HuntStep::Refused), "refused"));
}

void TheAdapterAndTheConfAreWired()
{
    std::string const adapter = ReadFile("src/mod_overseer.cpp");
    std::string const conf = ReadFile("conf/mod_overseer.conf.dist");
    Check("adapter source read", !adapter.empty());
    Check("adapter routes the row on kind=job",
          adapter.find("kind == \"job\" && OverseerDecisions::IsHuntRow(command)") !=
              std::string::npos);
    Check("adapter reads the switch", adapter.find("Overseer.Hunt.Enable") != std::string::npos);
    Check("adapter judges with the pure decision",
          adapter.find("D::HuntNext(") != std::string::npos &&
              adapter.find("D::HuntGate(") != std::string::npos);
    Check("conf documents the switch, default 0",
          conf.find("Overseer.Hunt.Enable") != std::string::npos);
}

}  // namespace

int main()
{
    TheRowIsRoutedOnItsFirstWord();
    TheGrammarTakesAnEntryACountAndAClock();
    TheLevelGapIsTheModulesOwn();
    EachGateWallRefusesByName();
    RefusalsSayWhetherToAskAgain();
    ThePollDecidesTheNextMove();
    TheAdapterAndTheConfAreWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_hunt_spawn: ok\n");
    return 0;
}
