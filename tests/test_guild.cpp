/*
 * The guild: what it covers, what it is short of, and who is worth asking.
 *
 * WHAT THIS FILE IS ACTUALLY PROTECTING, because it is not "the functions
 * return the right values". It is the claim in the header that this module does
 * not score players. Every assertion below is about a HOLE - a profession
 * nobody holds, a service no class brings, a role nobody can fill - and none of
 * them is about one candidate being better than another. If somebody later adds
 * a number that sorts characters by gear, or by money, or by played time, the
 * shape of these tests is what should make that look out of place.
 *
 * THE FAMILY BELOW IS THE REAL ONE, and its shape is why several of these cases
 * are interesting rather than arbitrary. Five characters, levels 34 to 38,
 * professions spread deliberately across ten of the eleven primaries. That
 * spread is what makes the profession gap exactly one row long, and a test
 * asserting "there are gaps" rather than "there is precisely this one" would
 * pass just as happily against a family rearranged into nonsense.
 *
 * It compiles src/overseer_decisions.cpp and this file and NOTHING ELSE, with
 * no include path into a core. If a core type ever gets into one of these
 * decisions, this stops compiling.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::ClassService;
using OverseerDecisions::GUILD_PROFESSION_COUNT;
using OverseerDecisions::GUILD_RANK_FOUNDER;
using OverseerDecisions::GUILD_RANK_MASTER;
using OverseerDecisions::GuildFormationAction;
using OverseerDecisions::GuildFormationFacts;
using OverseerDecisions::GuildFormationState;
using OverseerDecisions::GuildFormationStep;
using OverseerDecisions::GuildLevelBand;
using OverseerDecisions::GuildMemberFacts;
using OverseerDecisions::GuildNeeds;
using OverseerDecisions::GuildNeedsFrom;
using OverseerDecisions::GuildProfessionGaps;
using OverseerDecisions::GuildProfessionView;
using OverseerDecisions::GuildRequest;
using OverseerDecisions::GuildRole;
using OverseerDecisions::GuildRoleGaps;
using OverseerDecisions::GuildService;
using OverseerDecisions::GuildServiceGaps;
using OverseerDecisions::GuildVerb;
using OverseerDecisions::NextGuildFormationStep;
using OverseerDecisions::ParseGuildRequest;
using OverseerDecisions::ProfessionCover;
using OverseerDecisions::ProfessionHolding;
using OverseerDecisions::ProfessionName;
using OverseerDecisions::RecruitBand;
using OverseerDecisions::RecruitCandidate;
using OverseerDecisions::RecruitNeed;
using OverseerDecisions::RecruitPick;
using OverseerDecisions::RecruitRefusal;
using OverseerDecisions::RecruitShortlist;
using OverseerDecisions::RecruitVerdict;
using OverseerDecisions::RecruitVerdictFor;

namespace
{

// The skill ids this module already prints beside every profession it logs,
// spelled here so a failure reads as a profession rather than as a number. The
// same habit as tests/test_professions.cpp, extended by the three the guild
// question needs and that one did not.
unsigned const BLACKSMITHING = 164;
unsigned const LEATHERWORKING = 165;
unsigned const ALCHEMY = 171;
unsigned const HERBALISM = 182;
unsigned const MINING = 186;
unsigned const TAILORING = 197;
unsigned const ENGINEERING = 202;
unsigned const ENCHANTING = 333;
unsigned const SKINNING = 393;
unsigned const JEWELCRAFTING = 755;
unsigned const INSCRIPTION = 773;

unsigned const WARRIOR = 1;
unsigned const PALADIN = 2;
unsigned const HUNTER = 3;
unsigned const ROGUE = 4;
unsigned const PRIEST = 5;
unsigned const SHAMAN = 7;
unsigned const MAGE = 8;
unsigned const WARLOCK = 9;
unsigned const DRUID = 11;

unsigned const HORDE = 1;
unsigned const ALLIANCE = 0;

int failures = 0;

void Check(char const* what, bool held)
{
    if (held)
        return;
    ++failures;
    std::printf("FAIL %s\n", what);
}

void CheckUnsigned(char const* what, unsigned got, unsigned want)
{
    if (got == want)
        return;
    ++failures;
    std::printf("FAIL %s: expected %u, got %u\n", what, want, got);
}

void CheckString(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    ++failures;
    std::printf("FAIL %s: expected [%s], got [%s]\n", what, want.c_str(), got.c_str());
}

GuildMemberFacts Member(char const* name, unsigned classId, unsigned level,
                        unsigned firstSkill, unsigned firstValue,
                        unsigned secondSkill, unsigned secondValue)
{
    GuildMemberFacts member;
    member.name = name;
    member.classId = classId;
    member.level = level;
    if (firstSkill != 0)
    {
        ProfessionHolding first;
        first.skill = firstSkill;
        first.value = firstValue;
        member.professions.push_back(first);
    }
    if (secondSkill != 0)
    {
        ProfessionHolding second;
        second.skill = secondSkill;
        second.value = secondValue;
        member.professions.push_back(second);
    }
    return member;
}

// The five, as they actually stand: levels 34 to 38, professions spread across
// ten of the eleven primaries, nobody holding Engineering.
std::vector<GuildMemberFacts> TheFamily()
{
    std::vector<GuildMemberFacts> family;
    family.push_back(Member("Grug", WARRIOR, 36, BLACKSMITHING, 180, MINING, 195));
    family.push_back(Member("Grog", PALADIN, 38, JEWELCRAFTING, 150, INSCRIPTION, 140));
    family.push_back(Member("Bork", ROGUE, 35, LEATHERWORKING, 170, SKINNING, 200));
    family.push_back(Member("Og", MAGE, 34, TAILORING, 165, ENCHANTING, 120));
    family.push_back(Member("Ugga", PRIEST, 37, ALCHEMY, 175, HERBALISM, 190));
    return family;
}

RecruitCandidate Candidate(char const* name, unsigned classId, unsigned level,
                           unsigned firstSkill, unsigned secondSkill)
{
    RecruitCandidate candidate;
    candidate.name = name;
    candidate.classId = classId;
    candidate.level = level;
    candidate.teamId = HORDE;
    if (firstSkill != 0)
    {
        ProfessionHolding first;
        first.skill = firstSkill;
        first.value = 100;
        candidate.professions.push_back(first);
    }
    if (secondSkill != 0)
    {
        ProfessionHolding second;
        second.skill = secondSkill;
        second.value = 100;
        candidate.professions.push_back(second);
    }
    return candidate;
}

// The needs the family actually has, at a roster target of fifteen - about what
// the guilds already on this realm carry.
GuildNeeds FamilyNeeds()
{
    return GuildNeedsFrom(TheFamily(), HORDE, 15, 5);
}

// -- the view ---------------------------------------------------------------

void TheViewHasARowPerProfessionWhetherOrNotAnybodyHoldsIt()
{
    std::vector<ProfessionCover> const view = GuildProfessionView(TheFamily());

    CheckUnsigned("one row per primary profession",
                  static_cast<unsigned>(view.size()), GUILD_PROFESSION_COUNT);
    CheckUnsigned("eleven primaries, not fourteen", GUILD_PROFESSION_COUNT, 11);

    // Every row carries its own name, so a caller never has to look one up.
    bool named = true;
    for (std::size_t i = 0; i < view.size(); ++i)
        named = named && view[i].name[0] != '\0';
    Check("every row names its profession", named);

    // An empty guild still produces the full view. This is the property that
    // makes the view usable before anybody has been recruited at all.
    std::vector<ProfessionCover> const empty = GuildProfessionView({});
    CheckUnsigned("an empty guild still has eleven rows",
                  static_cast<unsigned>(empty.size()), GUILD_PROFESSION_COUNT);
    CheckUnsigned("and eleven gaps",
                  static_cast<unsigned>(GuildProfessionGaps(empty).size()),
                  GUILD_PROFESSION_COUNT);
}

void TheViewNamesWhoCanMakeThingsAndHowGoodTheyAre()
{
    std::vector<ProfessionCover> const view = GuildProfessionView(TheFamily());

    bool foundSmith = false;
    bool foundEngineering = false;
    for (std::size_t i = 0; i < view.size(); ++i)
    {
        if (view[i].skill == BLACKSMITHING)
        {
            foundSmith = true;
            CheckUnsigned("one blacksmith",
                          static_cast<unsigned>(view[i].holders.size()), 1);
            CheckString("and it is the tank",
                        view[i].holders.empty() ? "" : view[i].holders[0], "Grug");
            CheckUnsigned("at the value he actually has", view[i].best, 180);
        }
        if (view[i].skill == ENGINEERING)
        {
            foundEngineering = true;
            Check("nobody is an engineer", view[i].holders.empty());
            CheckUnsigned("so the best value in it is nothing", view[i].best, 0);
        }
    }
    Check("the view has a blacksmithing row", foundSmith);
    Check("the view has an engineering row even though it is empty", foundEngineering);

    CheckString("a skill that is not a profession is named as such",
                ProfessionName(43), "");
    CheckString("cooking is secondary and therefore not in this list",
                ProfessionName(185), "");
}

// -- what the family is short of --------------------------------------------

void TheFamilysOnlyProfessionHoleIsEngineering()
{
    std::vector<unsigned> const gaps =
        GuildProfessionGaps(GuildProfessionView(TheFamily()));

    // THIS IS THE MEASUREMENT THE WHOLE FEATURE TURNS ON. Ten of eleven
    // covered, by five characters holding two each, which is a deliberate
    // spread rather than an accident - and exactly one hole left in it.
    CheckUnsigned("exactly one profession hole",
                  static_cast<unsigned>(gaps.size()), 1);
    CheckUnsigned("and it is Engineering", gaps.empty() ? 0 : gaps[0], ENGINEERING);
}

void TheFamilyAlreadyCoversEveryRole()
{
    std::vector<GuildRole> const gaps = GuildRoleGaps(TheFamily());

    // Warrior and paladin tank, paladin and priest heal, mage and priest are
    // ranged, warrior and rogue are melee. A five-character family covering all
    // four is worth pinning, because it is the reason the recruiting rule does
    // NOT lead with roles: there is nothing there to lead with.
    CheckUnsigned("no role gaps", static_cast<unsigned>(gaps.size()), 0);

    // And the four are still reachable, so the absence above is a fact about
    // this family rather than a function that never returns anything.
    std::vector<GuildMemberFacts> justARogue;
    justARogue.push_back(Member("Bork", ROGUE, 35, LEATHERWORKING, 170, SKINNING, 200));
    CheckUnsigned("a lone rogue is short of three roles",
                  static_cast<unsigned>(GuildRoleGaps(justARogue).size()), 3);
}

void TheFamilyBringsNoneOfTheThreeServices()
{
    std::vector<GuildService> const gaps = GuildServiceGaps(TheFamily());

    CheckUnsigned("all three services are missing",
                  static_cast<unsigned>(gaps.size()), 3);
    Check("no warlock, so no summoning",
          ClassService(WARLOCK) == GuildService::Summoning);
    Check("no druid, so no battle res",
          ClassService(DRUID) == GuildService::BattleRes);
    Check("no shaman, so no bloodlust",
          ClassService(SHAMAN) == GuildService::Bloodlust);
    Check("and a class with no unique service says so",
          ClassService(ROGUE) == GuildService::None);

    // The family HAS a mage, and a mage is deliberately not on the service
    // list. If somebody adds Portals to that enum, this fails and they have to
    // read the comment saying why it is not there.
    std::vector<GuildMemberFacts> withAWarlock = TheFamily();
    withAWarlock.push_back(Member("Zug", WARLOCK, 36, 0, 0, 0, 0));
    CheckUnsigned("one warlock closes exactly one of the three",
                  static_cast<unsigned>(GuildServiceGaps(withAWarlock).size()), 2);
}

void TheBandIsBuiltRoundTheGuildAndDoesNotWrap()
{
    RecruitBand const band = GuildLevelBand(TheFamily(), 5);
    CheckUnsigned("the floor is the lowest member less the spread", band.lowest, 29);
    CheckUnsigned("the ceiling is the highest plus it", band.highest, 43);

    // The unsigned trap: a spread wider than the guild's own lowest level must
    // clamp at 1 rather than wrapping to near four billion, which would be a
    // gate that silently admits nobody.
    std::vector<GuildMemberFacts> lowbies;
    lowbies.push_back(Member("Pip", ROGUE, 3, SKINNING, 10, 0, 0));
    RecruitBand const clamped = GuildLevelBand(lowbies, 10);
    CheckUnsigned("the floor clamps at 1 rather than wrapping", clamped.lowest, 1);
    CheckUnsigned("the ceiling is unaffected", clamped.highest, 13);

    RecruitBand const none = GuildLevelBand({}, 5);
    CheckUnsigned("an empty guild has no floor", none.lowest, 0);
    CheckUnsigned("and no ceiling", none.highest, 0);
}

// -- the gates ---------------------------------------------------------------

void SomebodyElsesGuildMemberIsNeverTaken()
{
    GuildNeeds const needs = FamilyNeeds();

    // The strongest candidate this rule could ever see: an engineer, in band,
    // right faction, closing the one profession hole the family has. Refused
    // anyway, because they already have a guild. THAT IS THE POACHING RULE, and
    // it is asserted against the best case rather than a weak one on purpose.
    RecruitCandidate engineer = Candidate("Sprok", HUNTER, 36, ENGINEERING, MINING);
    engineer.guildId = 41;

    RecruitVerdict const verdict = RecruitVerdictFor(engineer, needs);
    Check("not invited", !verdict.invite);
    Check("and the reason is the guild they already have",
          verdict.refusal == RecruitRefusal::AlreadyGuilded);
    Check("with a sentence a person can read", !verdict.said.empty());

    // The same character with no guild is the one this rule wants most.
    engineer.guildId = 0;
    RecruitVerdict const free = RecruitVerdictFor(engineer, needs);
    Check("unguilded, the same character is invited", free.invite);
    Check("for the profession", free.need == RecruitNeed::Profession);
    CheckUnsigned("and it names Engineering", free.closes, ENGINEERING);
}

void TheGatesAreAskedInOrderAndEveryRefusalNamesItself()
{
    GuildNeeds const needs = FamilyNeeds();

    RecruitCandidate gone = Candidate("Husk", HUNTER, 36, ENGINEERING, 0);
    gone.gone = true;
    Check("a deleted character is refused",
          RecruitVerdictFor(gone, needs).refusal == RecruitRefusal::Gone);

    RecruitCandidate stranger = Candidate("Elar", HUNTER, 36, ENGINEERING, 0);
    stranger.teamId = ALLIANCE;
    Check("the other faction is refused, because the core would refuse it too",
          RecruitVerdictFor(stranger, needs).refusal == RecruitRefusal::OtherFaction);

    RecruitCandidate low = Candidate("Tiny", HUNTER, 8, ENGINEERING, 0);
    Check("too low to go where the guild goes",
          RecruitVerdictFor(low, needs).refusal == RecruitRefusal::BelowBand);

    RecruitCandidate high = Candidate("Tall", HUNTER, 70, ENGINEERING, 0);
    Check("too high to be doing what it does",
          RecruitVerdictFor(high, needs).refusal == RecruitRefusal::AboveBand);

    // Joinable, in band, right faction, and brings nothing: a rogue, when the
    // guild already has one and has no role or service hole a rogue could fill.
    // NOT A JUDGEMENT ABOUT THE CHARACTER, and the refusals are worded that way.
    RecruitCandidate const spare = Candidate("Snik", ROGUE, 36, MINING, SKINNING);

    GuildNeeds full = FamilyNeeds();
    full.targetSize = 5;  // no room for depth
    Check("a full roster refuses on the roster, not on the character",
          RecruitVerdictFor(spare, full).refusal == RecruitRefusal::RosterFull);

    GuildNeeds noTarget = FamilyNeeds();
    noTarget.targetSize = 0;  // no bench wanted at all
    Check("and with no bench wanted, the reason is that nothing is missing",
          RecruitVerdictFor(spare, noTarget).refusal == RecruitRefusal::NothingMissing);

    GuildNeeds roomy = FamilyNeeds();
    roomy.targetSize = 6;
    RecruitVerdict const bench = RecruitVerdictFor(spare, roomy);
    Check("with room, the same character is bench depth", bench.invite);
    Check("and the need says so", bench.need == RecruitNeed::Depth);

    // Every answer this function can give carries a sentence, on an invitation
    // and on a refusal alike. A name that appears on a list without a reason,
    // or vanishes from one without a reason, is the next bug.
    RecruitCandidate const cases[] = {gone, stranger, low, high, spare};
    bool allSaid = true;
    for (std::size_t i = 0; i < 5; ++i)
        allSaid = allSaid && !RecruitVerdictFor(cases[i], needs).said.empty();
    Check("every verdict says why", allSaid);
}

void TheNeedTiersAreInTheOrderTheHeaderClaims()
{
    GuildNeeds const needs = FamilyNeeds();

    // A warlock with no professions: closes a service and nothing else.
    RecruitCandidate const summoner = Candidate("Zug", WARLOCK, 36, 0, 0);
    RecruitVerdict const service = RecruitVerdictFor(summoner, needs);
    Check("a warlock is invited", service.invite);
    Check("for the service it alone brings", service.need == RecruitNeed::Service);

    // The same warlock holding Engineering is invited for the PROFESSION, the
    // higher tier. This is the one contestable judgement in the section and it
    // is pinned here so reordering it is a visible change rather than a quiet
    // one.
    RecruitCandidate const both = Candidate("Zug", WARLOCK, 36, ENGINEERING, 0);
    Check("profession beats service",
          RecruitVerdictFor(both, needs).need == RecruitNeed::Profession);
    Check("and the enum agrees with the prose",
          RecruitNeed::Profession > RecruitNeed::Service
              && RecruitNeed::Service > RecruitNeed::Role
              && RecruitNeed::Role > RecruitNeed::Depth);

    // A role gap only outranks depth. Built against a one-member guild so there
    // is a role hole to close at all.
    std::vector<GuildMemberFacts> justAMage;
    justAMage.push_back(Member("Og", MAGE, 34, TAILORING, 165, ENCHANTING, 120));
    GuildNeeds const thin = GuildNeedsFrom(justAMage, HORDE, 10, 5);
    RecruitCandidate const tank = Candidate("Grug", WARRIOR, 34, 0, 0);
    Check("a tank for a guild with none is invited for the role",
          RecruitVerdictFor(tank, thin).need == RecruitNeed::Role);
}

// -- the shortlist -----------------------------------------------------------

void OneHoleTakesOneCandidateAndNotThree()
{
    GuildNeeds const needs = FamilyNeeds();

    // Three engineers against one engineering hole. The first is taken FOR the
    // hole; the other two have to earn their place some other way, because
    // after the first pick the hole is closed. Running the verdict over each
    // candidate independently and sorting would invite all three for the same
    // reason.
    std::vector<RecruitCandidate> candidates;
    candidates.push_back(Candidate("Cogwin", HUNTER, 36, ENGINEERING, MINING));
    candidates.push_back(Candidate("Boltz", HUNTER, 35, ENGINEERING, MINING));
    candidates.push_back(Candidate("Sprok", HUNTER, 37, ENGINEERING, MINING));

    std::vector<RecruitPick> const picks = RecruitShortlist(candidates, needs, 3);
    CheckUnsigned("all three are still worth having",
                  static_cast<unsigned>(picks.size()), 3);

    unsigned professionPicks = 0;
    for (std::size_t i = 0; i < picks.size(); ++i)
        if (picks[i].verdict.need == RecruitNeed::Profession)
            ++professionPicks;
    CheckUnsigned("but only one of them is the engineer this guild needed",
                  professionPicks, 1);

    // And it is the first by name, because nothing here can tell three level-36
    // hunter engineers apart and the rule refuses to pretend otherwise.
    CheckString("ties break by name, so the answer is the same every run",
                candidates[picks[0].index].name, "Boltz");
}

void APickClosesEveryHoleItFillsAndNotOnlyTheNamedOne()
{
    GuildNeeds const needs = FamilyNeeds();

    // An engineer who is also a warlock is picked for the profession, the
    // higher tier. The summoning hole has to close with them anyway, or the
    // next warlock is invited for a service this guild now has.
    std::vector<RecruitCandidate> candidates;
    candidates.push_back(Candidate("Zug", WARLOCK, 36, ENGINEERING, 0));
    candidates.push_back(Candidate("Morg", WARLOCK, 35, 0, 0));

    std::vector<RecruitPick> const picks = RecruitShortlist(candidates, needs, 2);
    CheckUnsigned("both are invited", static_cast<unsigned>(picks.size()), 2);
    Check("the first for the profession",
          picks[0].verdict.need == RecruitNeed::Profession);
    CheckString("and it is the one holding it",
                candidates[picks[0].index].name, "Zug");
    Check("the second is not invited for a service the first already brought",
          picks[1].verdict.need != RecruitNeed::Service);
}

void TheShortlistStopsAtTheCapAndAtTheRoster()
{
    GuildNeeds const needs = FamilyNeeds();

    std::vector<RecruitCandidate> candidates;
    candidates.push_back(Candidate("Cogwin", HUNTER, 36, ENGINEERING, MINING));
    candidates.push_back(Candidate("Zug", WARLOCK, 36, 0, 0));
    candidates.push_back(Candidate("Faun", DRUID, 35, 0, 0));
    candidates.push_back(Candidate("Thrak", SHAMAN, 37, 0, 0));

    CheckUnsigned("the cap binds",
                  static_cast<unsigned>(RecruitShortlist(candidates, needs, 2).size()), 2);
    CheckUnsigned("a cap of nothing invites nobody",
                  static_cast<unsigned>(RecruitShortlist(candidates, needs, 0).size()), 0);

    // The family is five, so a target of seven leaves room for two.
    GuildNeeds small = FamilyNeeds();
    small.targetSize = 7;
    CheckUnsigned("and so does the roster target",
                  static_cast<unsigned>(RecruitShortlist(candidates, small, 10).size()), 2);

    // Everybody already guilded is nobody to invite, however many of them there
    // are. This is the realm's already-guilded population in miniature.
    std::vector<RecruitCandidate> guilded = candidates;
    for (std::size_t i = 0; i < guilded.size(); ++i)
        guilded[i].guildId = 41 + static_cast<unsigned>(i);
    CheckUnsigned("a pool of other guilds members yields an empty shortlist",
                  static_cast<unsigned>(RecruitShortlist(guilded, needs, 10).size()), 0);
}

void TheShortlistTakesTheHolesInTheOrderTheyMatter()
{
    GuildNeeds const needs = FamilyNeeds();

    // Deliberately listed worst-first, so an implementation that simply walked
    // the input would get this wrong.
    std::vector<RecruitCandidate> candidates;
    candidates.push_back(Candidate("Snik", ROGUE, 36, MINING, SKINNING));       // depth
    candidates.push_back(Candidate("Thrak", SHAMAN, 37, 0, 0));                 // service
    candidates.push_back(Candidate("Cogwin", HUNTER, 36, ENGINEERING, MINING)); // profession

    std::vector<RecruitPick> const picks = RecruitShortlist(candidates, needs, 3);
    CheckUnsigned("three picks", static_cast<unsigned>(picks.size()), 3);
    Check("the engineer first", picks[0].verdict.need == RecruitNeed::Profession);
    Check("then the shaman", picks[1].verdict.need == RecruitNeed::Service);
    Check("and the rogue last, as depth", picks[2].verdict.need == RecruitNeed::Depth);
    CheckString("the engineer is named", candidates[picks[0].index].name, "Cogwin");
    CheckString("the shaman is named", candidates[picks[1].index].name, "Thrak");
}

// -- forming it --------------------------------------------------------------

void FormationWaitsForTheFounderBeforeItTriesToCreate()
{
    GuildFormationState state;
    state.wantedName = "Stonefist Kin";
    state.founderName = "Grog";
    state.founders = {"Grog", "Grug", "Bork", "Og", "Ugga"};

    GuildFormationFacts facts;  // nothing exists, nobody is online

    GuildFormationAction const waiting = NextGuildFormationStep(state, facts);
    Check("an offline founder means waiting, not failing",
          waiting.step == GuildFormationStep::WaitForFounder);
    CheckString("and it says why", waiting.said, OverseerDecisions::GuildSaid::FounderOffline);

    facts.founderOnline = true;
    GuildFormationAction const create = NextGuildFormationStep(state, facts);
    Check("once online, create", create.step == GuildFormationStep::Create);
    CheckString("under the founder", create.who, "Grog");
    CheckUnsigned("who is guild master", create.rank, GUILD_RANK_MASTER);
}

void FormationAddsOneFounderAtATimeAndThenStops()
{
    GuildFormationState state;
    state.wantedName = "Stonefist Kin";
    state.founderName = "Grog";
    state.founders = {"Grog", "Grug", "Bork", "Og", "Ugga"};

    GuildFormationFacts facts;
    facts.founderOnline = true;
    facts.ours = true;             // created; Guild::Create added the master
    facts.alreadyIn = {"Grog"};

    // Walk it to stillness the way the profession test walks its assignment:
    // apply what each step says, ask again, and prove it terminates.
    unsigned steps = 0;
    for (; steps < 20; ++steps)
    {
        GuildFormationAction const action = NextGuildFormationStep(state, facts);
        if (action.step == GuildFormationStep::Nothing)
            break;
        Check("every step on the way is an add",
              action.step == GuildFormationStep::Add);
        CheckUnsigned("added as a founder, which is officer", action.rank,
                      GUILD_RANK_FOUNDER);
        facts.alreadyIn.push_back(action.who);
    }
    CheckUnsigned("four founders besides the guild master", steps, 4);
    CheckUnsigned("and all five are in",
                  static_cast<unsigned>(facts.alreadyIn.size()), 5);

    // Asked again, forever, it does nothing. The same idempotence the profession
    // step has, for the same reason: re-running formation must not re-add
    // anybody.
    GuildFormationAction const settled = NextGuildFormationStep(state, facts);
    Check("and it stays done", settled.step == GuildFormationStep::Nothing);
    CheckString("saying so", settled.said, OverseerDecisions::GuildSaid::Formed);

    // A founder who joined by some other route is skipped rather than added
    // twice: only the MISSING half of the comparison can cause anything.
    GuildFormationFacts partial;
    partial.founderOnline = true;
    partial.ours = true;
    partial.alreadyIn = {"Grog", "Grug", "Og"};
    GuildFormationAction const next = NextGuildFormationStep(state, partial);
    Check("the next add is somebody who is not in yet",
          next.step == GuildFormationStep::Add);
    CheckString("and it is the first such, in the order given", next.who, "Bork");
}

void FormationRefusesLoudlyRatherThanWorkingRound()
{
    GuildFormationState state;
    state.wantedName = "Stonefist Kin";
    state.founderName = "Grog";
    state.founders = {"Grog"};

    GuildFormationFacts taken;
    taken.founderOnline = true;
    taken.nameTakenByAnother = true;
    Check("a name somebody else holds is blocked, not worked round",
          NextGuildFormationStep(state, taken).step == GuildFormationStep::Blocked);

    GuildFormationFacts elsewhere;
    elsewhere.founderOnline = true;
    elsewhere.founderInAnotherGuild = true;
    Check("a founder who has joined another guild is blocked",
          NextGuildFormationStep(state, elsewhere).step == GuildFormationStep::Blocked);

    GuildFormationFacts online;
    online.founderOnline = true;

    GuildFormationState nameless = state;
    nameless.wantedName = "";
    Check("a guild with no name is blocked",
          NextGuildFormationStep(nameless, online).step == GuildFormationStep::Blocked);

    GuildFormationState leaderless = state;
    leaderless.founderName = "";
    Check("a guild with no founder is blocked",
          NextGuildFormationStep(leaderless, online).step == GuildFormationStep::Blocked);

    // And every block says which one it was.
    CheckString("a block always says which block it was",
                NextGuildFormationStep(state, taken).said,
                OverseerDecisions::GuildSaid::NameTaken);
    CheckString("and the other one names itself too",
                NextGuildFormationStep(state, elsewhere).said,
                OverseerDecisions::GuildSaid::FounderElsewhere);
}

// -- the row -----------------------------------------------------------------

void TheFiveVerbsParse()
{
    Check("view parses", ParseGuildRequest("view").verb == GuildVerb::View);
    Check("invite parses", ParseGuildRequest("invite").verb == GuildVerb::Invite);
    // The character is in target_arg, so a row that also names one here is
    // saying it twice rather than saying it wrong.
    Check("and a second mention of the name is not a refusal",
          ParseGuildRequest("invite Cogwin").verb == GuildVerb::Invite);

    GuildRequest const form = ParseGuildRequest("form Stonefist Kin");
    Check("form parses", form.verb == GuildVerb::Form);
    CheckString("and keeps the whole name, spaces and all", form.name, "Stonefist Kin");

    GuildRequest const shortlist = ParseGuildRequest("shortlist");
    Check("shortlist parses", shortlist.verb == GuildVerb::Shortlist);
    CheckUnsigned("with a default a person can read", shortlist.atMost,
                  OverseerDecisions::GUILD_SHORTLIST_DEFAULT);
    CheckUnsigned("and a count when one is given",
                  ParseGuildRequest("shortlist 3").atMost, 3);

    // Case and surrounding blanks do not matter, the same as every other row
    // this module parses.
    Check("the verb is case insensitive",
          ParseGuildRequest("VIEW").verb == GuildVerb::View);
    Check("and blanks do not matter",
          ParseGuildRequest("   view   ").verb == GuildVerb::View);
    CheckString("a name is trimmed but not otherwise touched",
                ParseGuildRequest("form   Stonefist Kin  ").name, "Stonefist Kin");

    GuildRequest const tabard = ParseGuildRequest("tabard 40 8 1 11 39");
    Check("tabard parses", tabard.verb == GuildVerb::Tabard);
    // In `guild` table order, which is the order EmblemInfo::LoadFromDB reads
    // them. Asserted one at a time rather than as a loop: a transposition
    // between border colour and emblem colour is exactly the bug that would
    // survive a loop comparing the array against itself.
    CheckUnsigned("emblem style first", tabard.emblem[0], 40);
    CheckUnsigned("then emblem colour", tabard.emblem[1], 8);
    CheckUnsigned("then border style", tabard.emblem[2], 1);
    CheckUnsigned("then border colour", tabard.emblem[3], 11);
    CheckUnsigned("and background last", tabard.emblem[4], 39);
}

void ATabardIsFiveBytesOrItIsRefused()
{
    using namespace OverseerDecisions::GuildRefusal;

    // ALL ZEROS IS A LEGAL TABARD and must not read as "no tabard given".
    // It is also the shape a guild has before anybody designs one, so a
    // parser that treated it as absent would make "reset it" unsayable.
    GuildRequest const plain = ParseGuildRequest("tabard 0 0 0 0 0");
    Check("all zeros is a tabard, not an empty row", plain.verb == GuildVerb::Tabard);
    CheckString("and is refused by nothing", plain.error, "");

    CheckString("four numbers is not a tabard",
                ParseGuildRequest("tabard 1 2 3 4").error, TabardNeedsFive);
    CheckString("nor is six", ParseGuildRequest("tabard 1 2 3 4 5 6").error,
                TabardNeedsFive);
    CheckString("nor is none at all", ParseGuildRequest("tabard").error,
                TabardNeedsFive);
    CheckString("a word among the numbers is refused",
                ParseGuildRequest("tabard 1 2 red 4 5").error, TabardNotANumber);
    CheckString("and so is a negative, which is the same refusal",
                ParseGuildRequest("tabard -1 2 3 4 5").error, TabardNotANumber);

    // THE ONE THAT MATTERS. The core stores each of these as a uint8, so 300
    // is not rejected anywhere downstream - it is written, read back as 44,
    // and the guild wears a tabard nobody chose.
    CheckString("a value wider than the byte it is stored in is refused",
                ParseGuildRequest("tabard 300 0 0 0 0").error, TabardValueTooBig);
    CheckString("and the ceiling itself is allowed",
                ParseGuildRequest("tabard 255 255 255 255 255").error, "");
    // Refused on the digit that overflows, so a very long run does not wrap
    // its way back into range before anybody looks at it.
    CheckString("a long run of digits is refused, not wrapped",
                ParseGuildRequest("tabard 4294967297 0 0 0 0").error,
                TabardValueTooBig);

    Check("extra blanks between the numbers do not matter",
          ParseGuildRequest("tabard  1   2  3 4   5 ").verb == GuildVerb::Tabard);
    Check("and the verb is case insensitive like the others",
          ParseGuildRequest("TABARD 1 2 3 4 5").verb == GuildVerb::Tabard);
}

void ABadRowIsRefusedByNameAndNeverSilently()
{
    using namespace OverseerDecisions::GuildRefusal;

    GuildRequest const nothing = ParseGuildRequest("");
    Check("an empty row is no verb", nothing.verb == GuildVerb::None);
    CheckString("and says which words it wanted", nothing.error, NoVerb);

    CheckString("an unknown verb is refused", ParseGuildRequest("disband").error, NoVerb);
    CheckString("form with no name is refused", ParseGuildRequest("form").error, NoName);
    CheckString("and a name longer than the core allows",
                ParseGuildRequest("form aaaaaaaaaaaaaaaaaaaaaaaaa").error, NameTooLong);
    // Exactly at the cap is fine. An off-by-one here would refuse a legal name
    // for ever, and the reason would read like a core rule.
    Check("exactly twenty four characters is a legal name",
          ParseGuildRequest("form aaaaaaaaaaaaaaaaaaaaaaaa").verb == GuildVerb::Form);

    CheckString("a shortlist of a name is refused",
                ParseGuildRequest("shortlist Cogwin").error, NotACount);
    CheckString("a shortlist of nothing is refused",
                ParseGuildRequest("shortlist 0").error, CountIsZero);
    CheckString("and one too large to answer",
                ParseGuildRequest("shortlist 5000").error, CountTooBig);
    // The overflow case, refused on the digit rather than after wrapping to
    // something small that would read like a deliberate answer.
    CheckString("a count that would wrap is refused before it does",
                ParseGuildRequest("shortlist 99999999999999999999").error, CountTooBig);

    // Every refusal literal goes into a column through an UPDATE, so none of
    // them may carry a quote.
    char const* const literals[] = {NoVerb,     NoName,      NameTooLong,
                                    NotACount,  CountTooBig, CountIsZero};
    bool clean = true;
    for (std::size_t i = 0; i < 6; ++i)
        for (char const* c = literals[i]; *c; ++c)
            clean = clean && *c != 0x27 && *c != 0x22 && *c != 0x5c;
    Check("no refusal literal carries a quote", clean);
}

}  // namespace

int main()
{
    TheViewHasARowPerProfessionWhetherOrNotAnybodyHoldsIt();
    TheViewNamesWhoCanMakeThingsAndHowGoodTheyAre();
    TheFamilysOnlyProfessionHoleIsEngineering();
    TheFamilyAlreadyCoversEveryRole();
    TheFamilyBringsNoneOfTheThreeServices();
    TheBandIsBuiltRoundTheGuildAndDoesNotWrap();
    SomebodyElsesGuildMemberIsNeverTaken();
    TheGatesAreAskedInOrderAndEveryRefusalNamesItself();
    TheNeedTiersAreInTheOrderTheHeaderClaims();
    OneHoleTakesOneCandidateAndNotThree();
    APickClosesEveryHoleItFillsAndNotOnlyTheNamedOne();
    TheShortlistStopsAtTheCapAndAtTheRoster();
    TheShortlistTakesTheHolesInTheOrderTheyMatter();
    FormationWaitsForTheFounderBeforeItTriesToCreate();
    FormationAddsOneFounderAtATimeAndThenStops();
    FormationRefusesLoudlyRatherThanWorkingRound();
    TheFiveVerbsParse();
    ATabardIsFiveBytesOrItIsRefused();
    ABadRowIsRefusedByNameAndNeverSilently();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the guild decisions hold\n");
    return EXIT_SUCCESS;
}
