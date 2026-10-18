/*
 * The three kinds of session an in-world roster character can have (#134).
 *
 * KeepRosterAttended spares a live client, hands a headless bot to the
 * headless rules, and kicks a real session whose socket has closed. That
 * sort was inlined in the sweep with no test; it is now
 * OverseerDecisions::ClassifyRosterSession, and each kind is asserted here,
 * along with the two "not yet" cases the sweep leaves to the core and the
 * order that keeps a client from ever being evicted.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::ClassifyRosterSession;
using OverseerDecisions::RosterSessionFacts;
using OverseerDecisions::RosterSessionKind;

namespace
{
int failures = 0;

char const* Name(RosterSessionKind kind)
{
    switch (kind)
    {
        case RosterSessionKind::LiveClient: return "LiveClient";
        case RosterSessionKind::HeadlessBot: return "HeadlessBot";
        case RosterSessionKind::LostClient: return "LostClient";
        case RosterSessionKind::Settling: return "Settling";
    }
    return "?";
}

void Check(char const* what, RosterSessionFacts const& facts, RosterSessionKind want)
{
    RosterSessionKind const got = ClassifyRosterSession(facts);
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

RosterSessionFacts Facts(bool clientAttached, bool hasSession, bool isBot, bool kicked)
{
    RosterSessionFacts facts;
    facts.clientAttached = clientAttached;
    facts.hasSession = hasSession;
    facts.isBotSession = isBot;
    facts.alreadyKicked = kicked;
    return facts;
}
}  // namespace

int main()
{
    // THE THREE KINDS.
    Check("a real client with an open socket is spared",
          Facts(true, true, false, false), RosterSessionKind::LiveClient);
    Check("a bot session with no client is headless",
          Facts(false, true, true, false), RosterSessionKind::HeadlessBot);
    Check("a real session whose socket closed has lost its client",
          Facts(false, true, false, false), RosterSessionKind::LostClient);

    // THE TWO "NOT YET" CASES: the core's own sweep is finishing them.
    Check("no session at all is mid-teardown",
          Facts(false, false, false, false), RosterSessionKind::Settling);
    Check("a lost client already marked kicked is not kicked twice",
          Facts(false, true, false, true), RosterSessionKind::Settling);

    // THE ORDER IS THE DECISION.
    Check("a live client is spared even if its session is marked kicked",
          Facts(true, true, false, true), RosterSessionKind::LiveClient);
    Check("a live client is spared whatever the bot flag says",
          Facts(true, true, true, false), RosterSessionKind::LiveClient);
    Check("a kicked bot is still a headless bot, never a lost client",
          Facts(false, true, true, true), RosterSessionKind::HeadlessBot);
    Check("no session wins over a stale bot flag",
          Facts(false, false, true, false), RosterSessionKind::Settling);

    // The default facts are the safe reading: nothing known means nothing done.
    Check("default facts settle", RosterSessionFacts{}, RosterSessionKind::Settling);

    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
