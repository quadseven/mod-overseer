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
using OverseerDecisions::RecruitBandFrom;
using OverseerDecisions::RecruitCandidate;
using OverseerDecisions::RecruitNeed;
using OverseerDecisions::RecruitPick;
using OverseerDecisions::RecruitPolicy;
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

// The policy the family recruits by in these tests. Deliberately NOT derived
// from the roster above: that derivation is the defect infra#3744 removed, and
// a fixture that reintroduced it would make every case below agree with a rule
// that no longer exists. The family sits at 34-38, and the band is written here
// as two numbers a person could have typed.
RecruitPolicy FamilyPolicy(unsigned targetSize = 15)
{
    RecruitPolicy policy;
    policy.levelMin = 29;
    policy.levelMax = 43;
    policy.targetSize = targetSize;
    return policy;
}

// The needs the family actually has, at a roster target of fifteen.
GuildNeeds FamilyNeeds()
{
    return GuildNeedsFrom(TheFamily(), HORDE, FamilyPolicy());
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

void TheBandIsThePolicyAndNotTheRoster()
{
    // THE WHOLE POINT OF infra#3744, asserted directly: the band is what the
    // policy says, and the guild's own levels do not enter into it. The family
    // used in these tests is levels 34-38; the policy below says 10-60 and the
    // band that comes out says 10-60.
    RecruitPolicy wide;
    wide.levelMin = 10;
    wide.levelMax = 60;
    RecruitBand const band = RecruitBandFrom(wide);
    CheckUnsigned("the floor is the configured floor", band.lowest, 10);
    CheckUnsigned("the ceiling is the configured ceiling", band.highest, 60);

    GuildNeeds const needs = GuildNeedsFrom(TheFamily(), HORDE, wide);
    CheckUnsigned("and the needs carry that band, not one built from members",
                  needs.band.lowest, 10);
    CheckUnsigned("at the top end too", needs.band.highest, 60);
    CheckUnsigned("the target size comes from the policy as well",
                  needs.targetSize, wide.targetSize);

    RecruitBand const none = RecruitBandFrom(RecruitPolicy{});
    CheckUnsigned("an unset policy has no floor", none.lowest, 0);
    CheckUnsigned("and no ceiling", none.highest, 0);
}

void AnUpsideDownBandAdmitsEverybodyRatherThanNobody()
{
    // A floor above the ceiling is a typo, and the two gates would refuse every
    // character alive. The only symptom would be an empty shortlist, which is
    // precisely the failure that took a day to find. So the ceiling is dropped:
    // recruiting too widely is a mistake somebody notices.
    RecruitPolicy inverted;
    inverted.levelMin = 55;
    inverted.levelMax = 20;
    RecruitBand const band = RecruitBandFrom(inverted);
    CheckUnsigned("the floor the operator typed is kept", band.lowest, 55);
    CheckUnsigned("the impossible ceiling is dropped rather than honoured",
                  band.highest, 0);
}

void EachEndOfTheBandGatesOnItsOwn()
{
    std::vector<GuildMemberFacts> const family = TheFamily();

    // A FLOOR WITH NO CEILING STILL REFUSES BELOW THE FLOOR. This used to be
    // wrong in the direction that matters: both gates hung off the CEILING
    // being set, so a floor-only policy admitted every level 1 character on the
    // realm - including, on the live realm, an unguilded level 1 DRUID
    // belonging to the auction-house bot.
    RecruitPolicy floorOnly;
    floorOnly.levelMin = 10;
    floorOnly.targetSize = 15;
    GuildNeeds const open = GuildNeedsFrom(family, HORDE, floorOnly);

    RecruitCandidate const tooLow = Candidate("Pip", DRUID, 1, 0, 0);
    Check("a level 1 is refused by a floor-only policy",
          RecruitVerdictFor(tooLow, open).refusal == RecruitRefusal::BelowBand);

    RecruitCandidate const veryHigh = Candidate("Zug", DRUID, 200, 0, 0);
    Check("and nothing is too high when no ceiling was set",
          RecruitVerdictFor(veryHigh, open).invite);

    // The mirror: a ceiling with no floor gates only at the top.
    RecruitPolicy ceilingOnly;
    ceilingOnly.levelMax = 60;
    ceilingOnly.targetSize = 15;
    GuildNeeds const capped = GuildNeedsFrom(family, HORDE, ceilingOnly);
    Check("a level 1 is admitted when no floor was set",
          RecruitVerdictFor(tooLow, capped).invite);
    Check("and the ceiling still refuses above it",
          RecruitVerdictFor(veryHigh, capped).refusal == RecruitRefusal::AboveBand);
}

void APolicyWideEnoughReachesThePopulationTheRosterRelativeBandMissed()
{
    // THE LIVE FAILURE, AS A TEST. Measured on wow-dev 2026-09-13: the family
    // stood at 43/45/47/47/48, the old rule built [38, 53] from that, and the
    // unguilded realm held 353 characters between 10 and 37 and NOT ONE between
    // 38 and 54. Every one of those 353 was refused BelowBand, and the only
    // thing anybody saw was an empty shortlist.
    std::vector<GuildMemberFacts> cave;
    cave.push_back(Member("Bork", ROGUE, 43, SKINNING, 12, 0, 0));
    cave.push_back(Member("Grog", PALADIN, 45, MINING, 1, 0, 0));
    cave.push_back(Member("Grug", WARRIOR, 47, MINING, 8, 0, 0));
    cave.push_back(Member("Og", MAGE, 47, TAILORING, 100, ENCHANTING, 90));
    cave.push_back(Member("Ugga", PRIEST, 48, HERBALISM, 132, 0, 0));

    // A level 22 druid: the shape of most of that 353, and a class that closes
    // one of the three service holes this family actually has.
    RecruitCandidate const lowDruid = Candidate("Sprout", DRUID, 22, 0, 0);

    RecruitPolicy asItWas;          // what [38, 53] amounted to
    asItWas.levelMin = 38;
    asItWas.levelMax = 53;
    asItWas.targetSize = 15;
    Check("the band that sat in the population hole refused them",
          RecruitVerdictFor(lowDruid, GuildNeedsFrom(cave, HORDE, asItWas))
              .refusal == RecruitRefusal::BelowBand);

    RecruitPolicy asShipped;        // the defaults this change ships
    asShipped.levelMin = 10;
    asShipped.levelMax = 60;
    asShipped.targetSize = 40;
    RecruitVerdict const now =
        RecruitVerdictFor(lowDruid, GuildNeedsFrom(cave, HORDE, asShipped));
    Check("the shipped defaults invite them", now.invite);
    Check("and for the battle resurrection nobody in the family brings",
          now.need == RecruitNeed::Service);

    // AND THE SIZE GATE MOVED WITH IT. Five members against a target of 15 left
    // ten reachable seats; against 40 it leaves thirty-five. This gates the
    // INVITE path, so it is the difference between a guild that can reach a
    // raid roster and one that refuses at fifteen however often it is asked.
    GuildNeeds const full = GuildNeedsFrom(cave, HORDE, asShipped);
    CheckUnsigned("the roster is five", full.memberCount, 5);
    CheckUnsigned("against a target of forty", full.targetSize, 40);
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
    GuildNeeds const thin = GuildNeedsFrom(justAMage, HORDE, FamilyPolicy(10));
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

void BankDepositParses()
{
    using namespace OverseerDecisions::GuildRefusal;

    GuildRequest const deposit = ParseGuildRequest("bank deposit 5000");
    Check("bank deposit parses", deposit.verb == GuildVerb::Bank);
    CheckUnsigned("and carries the copper amount", deposit.depositCopper, 5000);
    CheckString("refused by nothing", deposit.error, "");

    Check("the verb is case insensitive like the others",
          ParseGuildRequest("BANK deposit 1").verb == GuildVerb::Bank);
    Check("blanks around the numbers do not matter",
          ParseGuildRequest("bank   deposit   1  ").verb == GuildVerb::Bank);

    CheckString("bank alone is refused", ParseGuildRequest("bank").error,
                BankNeedsDeposit);
    CheckString("an unknown sub-verb is refused",
                ParseGuildRequest("bank withdraw 5000").error, BankNeedsDeposit);
    CheckString("deposit with nothing after it is refused",
                ParseGuildRequest("bank deposit").error, BankAmountNotANumber);
    CheckString("a word instead of a number is refused",
                ParseGuildRequest("bank deposit lots").error, BankAmountNotANumber);
    CheckString("a negative amount is the same refusal",
                ParseGuildRequest("bank deposit -5").error, BankAmountNotANumber);
    CheckString("trailing words after the amount are refused",
                ParseGuildRequest("bank deposit 500 gold").error,
                BankAmountNotANumber);
    CheckString("a deposit of zero is refused", ParseGuildRequest("bank deposit 0").error,
                BankAmountIsZero);
    CheckString("an amount past what a character can hold is refused",
                ParseGuildRequest("bank deposit 99999999999").error,
                BankAmountTooBig);
    // Refused on the digit that would overflow, not after wrapping to
    // something small that would read like a deliberate deposit.
    CheckString("a very long run of digits is refused, not wrapped",
                ParseGuildRequest("bank deposit 999999999999999999999999").error,
                BankAmountTooBig);
    CheckString("exactly the ceiling is allowed",
                ParseGuildRequest("bank deposit 2147483646").error, "");
}

void BankDepositItemParses()
{
    using namespace OverseerDecisions::GuildRefusal;

    GuildRequest const byGuid = ParseGuildRequest("bank deposit-item guid:494263");
    Check("deposit-item by guid parses", byGuid.verb == GuildVerb::BankDepositItem);
    Check("and is marked as a guid", byGuid.itemByGuid);
    CheckUnsigned("carrying the guid", byGuid.itemKey, 494263);
    CheckString("refused by nothing", byGuid.error, "");

    GuildRequest const byEntry = ParseGuildRequest("bank deposit-item entry:4562");
    Check("deposit-item by entry parses", byEntry.verb == GuildVerb::BankDepositItem);
    Check("and is NOT marked as a guid", !byEntry.itemByGuid);
    CheckUnsigned("carrying the entry", byEntry.itemKey, 4562);

    Check("the verb is case insensitive like the others",
          ParseGuildRequest("BANK deposit-item guid:1").verb == GuildVerb::BankDepositItem);
    Check("blanks around the spec do not matter",
          ParseGuildRequest("bank   deposit-item   guid:1  ").verb
              == GuildVerb::BankDepositItem);

    CheckString("no colon at all is refused",
                ParseGuildRequest("bank deposit-item 494263").error, BankItemSpecInvalid);
    CheckString("neither guid nor entry is refused",
                ParseGuildRequest("bank deposit-item stack:494263").error,
                BankItemSpecInvalid);
    CheckString("a non-numeric value is refused",
                ParseGuildRequest("bank deposit-item guid:abc").error, BankItemSpecInvalid);
    CheckString("a key of zero is refused",
                ParseGuildRequest("bank deposit-item guid:0").error, BankItemSpecInvalid);
    CheckString("no spec at all is refused",
                ParseGuildRequest("bank deposit-item").error, BankItemSpecInvalid);
    CheckString("trailing words after the spec are refused",
                ParseGuildRequest("bank deposit-item guid:5 extra").error,
                BankItemSpecInvalid);
    CheckString("an amount past uint32 is refused, not wrapped",
                ParseGuildRequest("bank deposit-item guid:99999999999").error,
                BankItemSpecInvalid);
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

    CheckString("raid with a word it does not know is refused",
                ParseGuildRequest("raid disband").error, RaidTakesFormOrNothing);

    // Every refusal literal goes into a column through an UPDATE, so none of
    // them may carry a quote.
    //
    // THE COUNT IS TAKEN FROM THE ARRAY AND NOT WRITTEN OUT. It used to be a
    // literal 10 beside an array of 10, which meant the four tabard literals
    // and the deposit-item one were never swept and nothing said so - a loop
    // bound that has to be kept in step with a list beside it is a loop bound
    // that will not be.
    char const* const literals[] = {NoVerb,
                                    NoName,
                                    NameTooLong,
                                    NotACount,
                                    CountTooBig,
                                    CountIsZero,
                                    BankNeedsDeposit,
                                    BankAmountNotANumber,
                                    BankAmountIsZero,
                                    BankAmountTooBig,
                                    BankItemSpecInvalid,
                                    TabardNeedsFive,
                                    TabardNotANumber,
                                    TabardValueTooBig,
                                    RaidTakesFormOrNothing};
    bool clean = true;
    for (std::size_t i = 0; i < sizeof(literals) / sizeof(literals[0]); ++i)
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
    TheBandIsThePolicyAndNotTheRoster();
    AnUpsideDownBandAdmitsEverybodyRatherThanNobody();
    EachEndOfTheBandGatesOnItsOwn();
    APolicyWideEnoughReachesThePopulationTheRosterRelativeBandMissed();
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
    BankDepositParses();
    BankDepositItemParses();
    ABadRowIsRefusedByNameAndNeverSilently();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the guild decisions hold\n");
    return EXIT_SUCCESS;
}
