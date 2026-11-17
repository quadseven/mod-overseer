/*
 * A family member lent to a guild run comes back after a bound.
 *
 * A guild run lives in module memory and ends on its own conditions. A family's
 * only healer lent to one stayed lent for the whole run, up to two and a half
 * hours, and the family's dungeon queue waited for it. LendBoundDue is the
 * decision the guild run's tick asks; the second half pins the wiring as text,
 * because a decision nothing asks is the failure this repository has shipped
 * before.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using OverseerDecisions::LEND_BOUND_SECONDS;
using OverseerDecisions::LendBoundDue;

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

void TheBoundIsFortyFiveMinutes()
{
    Check("the default bound is 2700 seconds", LEND_BOUND_SECONDS == 2700);
}

void OnlyALentMemberIsDue()
{
    std::int64_t const start = 1000000;
    Check("a lent member past the bound is due",
          LendBoundDue(true, start, start + 2701, LEND_BOUND_SECONDS));
    Check("a member that was not lent is never due",
          !LendBoundDue(false, start, start + 9000, LEND_BOUND_SECONDS));
}

void TheBoundIsStrict()
{
    std::int64_t const start = 1000000;
    Check("inside the bound is not due",
          !LendBoundDue(true, start, start + 2699, LEND_BOUND_SECONDS));
    Check("exactly at the bound is not due",
          !LendBoundDue(true, start, start + 2700, LEND_BOUND_SECONDS));
    Check("one second past is due", LendBoundDue(true, start, start + 2701, LEND_BOUND_SECONDS));
}

void BadInputsFailClosed()
{
    Check("a bound of 0 turns the bound off", !LendBoundDue(true, 100, 999999, 0));
    Check("an unset start never releases", !LendBoundDue(true, 0, 999999, 2700));
    Check("a clock behind the start never releases", !LendBoundDue(true, 5000, 4000, 2700));
}

std::string Slurp(char const* path)
{
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void TheGuildRunTickAsksIt()
{
    std::string const src = Slurp("src/mod_overseer.cpp");
    Check("the module source is readable", !src.empty());
    Check("the tick asks LendBoundDue", src.find("OverseerDecisions::LendBoundDue(") != std::string::npos);
    Check("the bound is a conf option",
          src.find("Overseer.GuildRun.LendBoundSeconds") != std::string::npos);
    Check("the option is read silently, so an unset key logs nothing each poll",
          src.find("OverseerDecisions::LEND_BOUND_SECONDS, false)") != std::string::npos);
    Check("the release logs the bound",
          src.find("lent member '{}' released after {}s (bound)") != std::string::npos);
    std::string const conf = Slurp("conf/mod_overseer.conf.dist");
    Check("the conf.dist documents the option",
          conf.find("Overseer.GuildRun.LendBoundSeconds") != std::string::npos);
}

}  // namespace

int main()
{
    TheBoundIsFortyFiveMinutes();
    OnlyALentMemberIsDue();
    TheBoundIsStrict();
    BadInputsFailClosed();
    TheGuildRunTickAsksIt();
    if (failures)
        return 1;
    std::printf("ok\n");
    return 0;
}
