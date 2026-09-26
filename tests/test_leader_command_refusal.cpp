/*
 * A family leader's movement is not toggled from outside the intent book.
 *
 * Measured on the dev realm over the 7 days to 2026-09-26: the bridge's
 * in-game ear queued `reset ai` 164 times, `follow` 89 times and `stay` 63
 * times, every one sourced `heard:<family bot>`, off lines the family's own
 * bots said ("I am out of reagents for prayer of shadow protection and am
 * casting shadow protection instead."). The leader got 18 of those `stay`s.
 * Nothing ever took `stay` off him again, and upstream's `stay` action stops a
 * moving bot on every tick its walk yields: walk a tick, stop, walk a tick.
 * The bridge's life rule also toggled `nc +new rpg` / `nc -new rpg` on him
 * every ten minutes (2,584 and 1,987 rows across the roster in the week).
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::CommandSourceIsOperator;
using OverseerDecisions::LeaderCommandRefusal;

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

bool Refused(bool leader, char const* source, char const* command)
{
    return !LeaderCommandRefusal(leader, source, command).empty();
}

}  // namespace

int main()
{
    Check("discord is the operator", CommandSourceIsOperator("discord:1234"));
    Check("the site is the operator", CommandSourceIsOperator("web:overseer"));
    Check("the in-game ear is not", !CommandSourceIsOperator("heard:Ugga"));
    Check("the life rule is not", !CommandSourceIsOperator("overseer:life"));
    Check("a kin muster is not", !CommandSourceIsOperator("kin:Og"));

    Check("the measured follow push to the leader is refused",
          Refused(true, "heard:Ugga", "follow"));
    Check("so is stay", Refused(true, "heard:Ugga", "stay"));
    Check("so is reset ai", Refused(true, "heard:Og", "reset ai"));
    Check("so is the life rule's new rpg grant", Refused(true, "overseer:life", "nc +new rpg"));
    Check("and its strip", Refused(true, "overseer:life", "nc -new rpg"));
    Check("and a follow toggle", Refused(true, "overseer:life", "nc +follow"));
    Check("any item of a list", Refused(true, "kin:Og", "nc +grind,+stay"));
    Check("case and spacing do not matter", Refused(true, "heard:Ugga", "  Follow "));

    Check("the flee strategy is not movement", !Refused(true, "overseer:life", "co +flee"));
    Check("a grind strategy is not the book's", !Refused(true, "overseer:life", "nc +grind"));
    Check("a follower may be told to follow", !Refused(false, "heard:Ugga", "follow"));
    Check("a follower keeps its life strategies",
          !Refused(false, "overseer:life", "nc -new rpg"));
    Check("the operator may tell the leader anything",
          !Refused(true, "discord:1234", "stay"));
    Check("including from the site", !Refused(true, "web:overseer", "follow"));

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a leader's movement is the intent book's\n");
    return EXIT_SUCCESS;
}
