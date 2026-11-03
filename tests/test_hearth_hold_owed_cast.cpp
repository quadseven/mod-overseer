/*
 * A `hearth` hold outside a death-spot episode either casts or says why it
 * cannot, before its ceiling (#1338).
 *
 * Read from the dev realm, 2026-10-30T19:13Z to 2026-10-31T19:13Z: 159 hearth
 * holds ended at the ceiling with "last refusal: none recorded; 0 such
 * hold(s)". 152 were placed by DoHearth refusing a hearth row from the bridge
 * because the character was moving (overseer:guildrun 124, guildwalk:ground
 * 13, overseer:town-errand 9, guildjobs 6). The bridge asks again every 60
 * seconds and the hold's ceiling is 45, so every re-ask found the character
 * walking again and nothing ever cast. 6 were death-spot keep-out holds whose
 * episode ended while they stood, and 1 was the revived-sickness hearth
 * (#746), also refused for moving.
 *
 * So a hold placed for a hearth refused only for moving now owes the cast:
 * once the character stands and nobody has asked again within a grace, the
 * module casts it, or logs the refusal and lets go. And every hearth hold
 * carries the last thing said about why it has no cast, so the ceiling line
 * names it.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, plus a read of
 * the module source for the wiring.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::DecideOwedHearth;
using OverseerDecisions::HEARTH_OWED_GRACE_SECONDS;
using OverseerDecisions::HearthCeilingRefusal;
using OverseerDecisions::HearthRefusalOwesACast;
using OverseerDecisions::OwedHearthFacts;
using OverseerDecisions::OwedHearthStep;

namespace
{

int failures = 0;

void Check(char const* what, bool got)
{
    if (got)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

void CheckStep(char const* what, OwedHearthStep got, OwedHearthStep want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %d, wanted %d\n", what, static_cast<int>(got),
                static_cast<int>(want));
    ++failures;
}

OwedHearthFacts Standing()
{
    OwedHearthFacts facts;
    facts.owed = true;
    facts.found = true;
    facts.moving = false;
    facts.sameMap = true;
    facts.secondsSinceAsked = HEARTH_OWED_GRACE_SECONDS;
    return facts;
}

void OnlyMovingOwesACast()
{
    Check("a hearth refused for moving owes the cast", HearthRefusalOwesACast("character is moving"));
    Check("a hearth refused in combat does not", !HearthRefusalOwesACast("character is in combat"));
    Check("a stone on cooldown does not", !HearthRefusalOwesACast("hearthstone is on cooldown"));
    Check("a cast that started does not", !HearthRefusalOwesACast(""));
}

void TheOwedCast()
{
    // The live case: the bridge's row was refused for moving, the hold stood
    // the character still, and no one asked again inside the grace.
    CheckStep("standing, past the grace: cast", DecideOwedHearth(Standing()),
              OwedHearthStep::Cast);

    OwedHearthFacts facts = Standing();
    facts.secondsSinceAsked = HEARTH_OWED_GRACE_SECONDS - 1;
    CheckStep("inside the grace the caller's own re-ask goes first", DecideOwedHearth(facts),
              OwedHearthStep::Wait);

    facts = Standing();
    facts.moving = true;
    CheckStep("still moving: wait, the ceiling names the refusal", DecideOwedHearth(facts),
              OwedHearthStep::Wait);

    facts = Standing();
    facts.sameMap = false;
    CheckStep("on another map the ask is stale and the hold lets go", DecideOwedHearth(facts),
              OwedHearthStep::Stale);

    facts = Standing();
    facts.rowOwnsIt = true;
    CheckStep("a cast in flight owns the hold", DecideOwedHearth(facts), OwedHearthStep::None);

    facts = Standing();
    facts.owed = false;
    CheckStep("a hold that owes no cast is left to its owner", DecideOwedHearth(facts),
              OwedHearthStep::None);

    facts = Standing();
    facts.found = false;
    CheckStep("a character not in the world is left to the ceiling", DecideOwedHearth(facts),
              OwedHearthStep::None);
}

void TheCeilingLineNamesWhy()
{
    Check("the episode's refusal comes first",
          HearthCeilingRefusal("character is in combat", "character is moving") ==
              "character is in combat");
    Check("outside an episode the hold's own refusal is named",
          HearthCeilingRefusal("", "character is moving") == "character is moving");
    Check("and only a hold with nothing said reads none recorded",
          HearthCeilingRefusal("", "") == "none recorded");
}

std::string Source()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string Body(std::string const& source, char const* signature)
{
    std::size_t const start = source.find(signature);
    if (start == std::string::npos)
        return "";
    std::size_t const brace = source.find('{', start);
    int depth = 1;
    std::size_t end = brace + 1;
    while (depth && end < source.size())
    {
        depth += (source[end] == '{') - (source[end] == '}');
        ++end;
    }
    return source.substr(start, end - start);
}

void TheModuleIsWired()
{
    std::string const source = Source();

    std::string const doHearth = Body(source, "static char const* DoHearth(");
    Check("DoHearth records each refusal on the hearth hold",
          doHearth.find("NoteHearthHold(") != std::string::npos);

    std::string const sweep = Body(source, "void CastOwedHearths()");
    Check("the owed-cast sweep exists", !sweep.empty());
    Check("it asks the decision", sweep.find("OverseerDecisions::DecideOwedHearth(") !=
                                      std::string::npos);
    Check("it casts through DoHearth",
          sweep.find("DoHearth(who, \"use\", status, evidence, _pendingHearths, 0)") !=
              std::string::npos);
    Check("a refused owed cast lets the hold go before its ceiling",
          sweep.find("ReleaseHold(") != std::string::npos);

    std::string const poll = Body(source, "void DeliverPendingCommands(");
    std::size_t const owed = poll.find("CastOwedHearths();");
    std::size_t const expiry = poll.find("ReleaseExpiredHolds();");
    Check("the poll casts owed hearths before the ceiling sweep",
          owed != std::string::npos && expiry != std::string::npos && owed < expiry);

    std::string const ceiling = Body(source, "void ReleaseExpiredHolds()");
    Check("the ceiling line names the hold's own refusal",
          ceiling.find("OverseerDecisions::HearthCeilingRefusal(") != std::string::npos);

    std::string const spot = Body(source, "bool LeaveGuildDeathSpot(");
    Check("the death-spot keep-out says why its hold has no cast on the hold",
          spot.find("NoteHearthHold(name,") != std::string::npos);
}

}  // namespace

int main()
{
    OnlyMovingOwesACast();
    TheOwedCast();
    TheCeilingLineNamesWhy();
    TheModuleIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok test_hearth_hold_owed_cast\n");
    return 0;
}
