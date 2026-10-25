/*
 * What a worldserver roll still left behind after #915 (wow-overseer#789,
 * layers 3 and 4).
 *
 * The live failure (dev realm, 2026-10-18): a roll released three guild finder
 * runs at the end of their adoption window because their tanks were never back
 * in the world. Their other members were back and inside, and an hour later
 * all three finder groups still held them: 10 online guild bots in groups no
 * run owned, skipped by the guild coordinator as already grouped. The stray
 * pass was keyed on the tank alone, so its clock never started and the group
 * was never found.
 *
 * And a read-back a roll ended read as a plain error, which a bridge pass that
 * backs off a refusal took for the bot saying no.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else, and reads
 * src/mod_overseer.cpp to pin the wiring (run from the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace OverseerDecisions;

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

GuildRunStrayMember Away()
{
    return GuildRunStrayMember{};
}

GuildRunStrayMember InTheLostGroup()
{
    GuildRunStrayMember m;
    m.inWorld = true;
    m.inGroup = true;
    m.finderGroup = true;
    m.tankSavedInGroup = true;
    return m;
}

// The 2026-10-18 shape: the tank never logs in, four of the five are back
// inside, still in the finder group the tank is saved in.
void ALostRunsGroupIsFoundWithoutItsTank()
{
    std::vector<GuildRunStrayMember> members{Away(), InTheLostGroup(), InTheLostGroup(),
                                             InTheLostGroup(), Away()};
    GuildRunStrayLook look = GuildRunStrayLookAt(members, false);
    Check("the clock may start with only the members back", look.anyInWorld);
    Check("the group is read through the first member back", look.witness == 1);
    Check("that member's group is the stray's finder group",
          look.facts.seenInGroup && look.facts.finderGroup && !look.facts.liveRunGroup);
    look.facts.expired = false;
    Check("so the living are taken out", GuildRunStrayNext(look.facts) == GuildRunStrayStep::TakeOut);
    look = GuildRunStrayLookAt(members, true);
    Check("and then the group is disbanded",
          GuildRunStrayNext(look.facts) == GuildRunStrayStep::Disband);

    // The clock starts at the first member's sight, so the group outlives the
    // run row by a quarter hour at most.
    std::int64_t const seen = 5000;
    std::int64_t const until = GuildRunStrayExpiry(look.anyInWorld, 0, seen);
    Check("the quarter hour starts at the first member's sight",
          until == seen + static_cast<std::int64_t>(GUILD_RUN_STRAY_SECONDS));
}

void NobodyBackStartsNoClock()
{
    std::vector<GuildRunStrayMember> members(5, Away());
    GuildRunStrayLook const look = GuildRunStrayLookAt(members, false);
    Check("nobody in the world: no clock", !look.anyInWorld);
    Check("nobody in the world: no group", look.witness == GUILD_RUN_STRAY_NOBODY);
    Check("nobody in the world: wait", GuildRunStrayNext(look.facts) == GuildRunStrayStep::Wait);
    Check("an empty stray reads as nobody",
          GuildRunStrayLookAt({}, false).witness == GUILD_RUN_STRAY_NOBODY);
}

void TheTankBackDecidesAsBefore()
{
    GuildRunStrayMember tank = InTheLostGroup();
    std::vector<GuildRunStrayMember> members{tank, InTheLostGroup()};
    GuildRunStrayLook look = GuildRunStrayLookAt(members, false);
    Check("the tank in the world is the witness", look.witness == 0);
    Check("its finder group is taken out", GuildRunStrayNext(look.facts) == GuildRunStrayStep::TakeOut);

    members[0].heldGroup = true;
    look = GuildRunStrayLookAt(members, false);
    Check("a tank in a live run's group drops the stray",
          look.facts.liveRunGroup && GuildRunStrayNext(look.facts) == GuildRunStrayStep::Drop);

    members[0] = GuildRunStrayMember{};
    members[0].inWorld = true;
    look = GuildRunStrayLookAt(members, false);
    Check("a tank back with no group waits, whatever its members' groups are",
          look.witness == 0 && GuildRunStrayNext(look.facts) == GuildRunStrayStep::Wait);
}

void AMemberNeverTakesAnotherGroup()
{
    // A member seated in a run this process drives, or one waiting to be
    // adopted: never the leftover, and never a reason to drop the stray.
    GuildRunStrayMember held = InTheLostGroup();
    held.heldGroup = true;
    std::vector<GuildRunStrayMember> members{Away(), held};
    GuildRunStrayLook look = GuildRunStrayLookAt(members, false);
    Check("a held group is not chosen through a member", look.witness == GUILD_RUN_STRAY_NOBODY);
    Check("and the stray waits rather than drops",
          GuildRunStrayNext(look.facts) == GuildRunStrayStep::Wait);

    // A finder group the tank is not saved in is another run's.
    GuildRunStrayMember other = InTheLostGroup();
    other.tankSavedInGroup = false;
    members = {Away(), other};
    look = GuildRunStrayLookAt(members, false);
    Check("a finder group without the tank is not the stray's", look.witness == GUILD_RUN_STRAY_NOBODY);

    // A plain party (a family's, a new guild party forming) is not a finder group.
    GuildRunStrayMember party = InTheLostGroup();
    party.finderGroup = false;
    members = {Away(), party};
    look = GuildRunStrayLookAt(members, false);
    Check("a plain party is never the leftover", look.witness == GUILD_RUN_STRAY_NOBODY);
    Check("but a member in the world still starts the clock", look.anyInWorld);
    look.facts.expired = true;
    Check("and once it runs out the stray is forgotten",
          GuildRunStrayNext(look.facts) == GuildRunStrayStep::Drop);

    // The first member that qualifies is the witness, past one that does not.
    members = {Away(), party, InTheLostGroup()};
    look = GuildRunStrayLookAt(members, false);
    Check("the witness is the first member in the lost group", look.witness == 2);
}

void ARestartsReadBackSaysRestart()
{
    std::string const detail = ClaimExpiredDetail();
    Check("the detail starts with the restart word", detail.rfind(RESTART_DETAIL_PREFIX, 0) == 0);
    Check("the word is 'restart: '", std::string(RESTART_DETAIL_PREFIX) == "restart: ");
    Check("it still says what happened",
          detail == "restart: claim expired: the run holding it is gone");
    Check("it fits the detail column", detail.size() <= 255);
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
    std::size_t const end = begin == std::string::npos ? begin : source.find(to, begin + 1);
    if (begin == std::string::npos || end == std::string::npos)
        return std::string();
    return source.substr(begin, end - begin);
}

bool Has(std::string const& in, char const* what)
{
    return in.find(what) != std::string::npos;
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

    std::string const strays =
        Between(source, "void DriveGuildRunStrays(std::time_t now)", "HOW OFTEN THE LAST RUNG");
    Check("the stray drive is found", !strays.empty());
    Check("the stray is read through all its members",
          Has(strays, "OverseerDecisions::GuildRunStrayLookAt(members, stray.teleported)"));
    Check("the clock starts on any member's sight",
          Has(strays, "OverseerDecisions::GuildRunStrayExpiry(look.anyInWorld, stray.until, now)"));
    Check("the group acted on is the witness's", Has(strays, "groups[look.witness]"));
    Check("the tank's saved membership is asked", Has(strays, "g->IsMember(tankGuid)"));
    Check("a waiting adoption's group is held", Has(strays, "for (GuildRunAdoption const& adoption : _guildRunAdoptions)"));
    Check("a row a roll already ended is read again",
          Has(strays, "OR (status = 'error' AND (detail LIKE 'lost at the restart:%' ") &&
              Has(strays, "OR detail LIKE 'restart:%') AND updated_at > NOW() - INTERVAL {} SECOND))"));
    Check("the take-out without the tank is said",
          Has(strays, "its finder group, read through '{}', is taken out"));
    Check("the tank alone no longer decides", !Has(strays, "Group* const group = tank ? tank->GetGroup() : nullptr;"));

    std::string const expire = Between(source, "void ExpireAbandonedClaims()", "void SayHowTheQueueIsDoing");
    Check("the claim sweep is found", !expire.empty());
    Check("it writes the restart detail",
          Has(expire, "Esc(OverseerDecisions::ClaimExpiredDetail())"));
    Check("the plain detail is gone", !Has(expire, "detail = 'claim expired: the run holding it is gone'"));
}

}  // namespace

int main()
{
    ALostRunsGroupIsFoundWithoutItsTank();
    NobodyBackStartsNoClock();
    TheTankBackDecidesAsBefore();
    AMemberNeverTakesAnotherGroup();
    ARestartsReadBackSaysRestart();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
