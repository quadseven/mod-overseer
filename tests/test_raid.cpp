/*
 * The raid: who is seated to do what, which of groups 1 through 8 they stand
 * in, and how a raid that is standing in the wrong groups is moved into the
 * right ones.
 *
 * WHAT THIS FILE IS ACTUALLY PROTECTING, because it is not "the function
 * returns eight groups". Three properties, and each one is a mistake that
 * would otherwise be found on a raid night:
 *
 *   1. A SEAT IS EXCLUSIVE. The recruit rule may say a paladin could tank and
 *      could heal; a seating plan may not. Every count asserted below adds up
 *      to the roster exactly once, and a plan that seated somebody twice, or
 *      lost them between the passes, changes one of those totals.
 *   2. LEVEL DECIDES WHO TAKES THE SCARCE SEAT. The seven warriors in the
 *      fixture are named so that their alphabetical order and their level
 *      order DISAGREE - see TheTanksAreTheHighestLevelWarriorsAndNotTheFirst
 *      Alphabetically. A tie-break that quietly reverted to name order, which
 *      is what the rest of this module does, would fail here rather than put a
 *      level 20 in front of a boss.
 *   3. THE MOVES ARE APPLIED AND COMPARED, NOT COUNTED. The seating test does
 *      not assert "some moves were emitted"; it runs them over an array and
 *      asserts the array afterwards equals the plan. A test that asserted on
 *      the move list itself would pass for a list that never arrived anywhere.
 *
 * THE FORTY-MEMBER FIXTURE IS THE REAL GUILD'S SHAPE. Its class distribution -
 * seven warriors, six priests, five mages, four each of paladin, hunter, rogue,
 * warlock and druid, one death knight, one shaman - was counted against the
 * live character database on 2026-09-13, and its levels span the same 13 to 55
 * the real roster does. The NAMES are invented, because a name adds nothing a
 * test needs and this repository is public. That distribution is what makes
 * several of these cases interesting rather than arbitrary: it is short of
 * shamans, deep in warriors, and holds exactly enough priests and paladins to
 * fill the healer quota without touching the druids.
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

using OverseerDecisions::GuildMemberFacts;
using OverseerDecisions::GuildRequest;
using OverseerDecisions::GuildVerb;
using OverseerDecisions::ParseGuildRequest;
using OverseerDecisions::RAID_HEALERS_DEFAULT;
using OverseerDecisions::RAID_SEAT_COUNT;
using OverseerDecisions::RAID_SIZE;
using OverseerDecisions::RAID_SUBGROUP_SIZE;
using OverseerDecisions::RAID_SUBGROUPS;
using OverseerDecisions::RAID_TANKS_DEFAULT;
using OverseerDecisions::PlanRaid;
using OverseerDecisions::RaidDamageBand;
using OverseerDecisions::RaidMove;
using OverseerDecisions::RaidPlan;
using OverseerDecisions::RaidSeat;
using OverseerDecisions::RaidSeatName;
using OverseerDecisions::RaidSeatingMoves;
using OverseerDecisions::RaidSeatNow;
using OverseerDecisions::RaidSeatPlan;
using OverseerDecisions::RaidShape;
using OverseerDecisions::RaidShapeFor;

namespace
{

// The core's own class ids, the same list tests/test_guild.cpp keeps and for
// the same reason: a failure should read as a class rather than as a number.
unsigned const WARRIOR = 1;
unsigned const PALADIN = 2;
unsigned const HUNTER = 3;
unsigned const ROGUE = 4;
unsigned const PRIEST = 5;
unsigned const DEATH_KNIGHT = 6;
unsigned const SHAMAN = 7;
unsigned const MAGE = 8;
unsigned const WARLOCK = 9;
unsigned const DRUID = 11;

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
    std::printf("FAIL %s: expected '%s', got '%s'\n", what, want.c_str(), got.c_str());
}

GuildMemberFacts Member(char const* name, unsigned classId, unsigned level)
{
    GuildMemberFacts member;
    member.name = name;
    member.classId = classId;
    member.level = level;
    return member;
}

RaidShape TheDefaultShape()
{
    RaidShape shape;
    shape.tanks = RAID_TANKS_DEFAULT;
    shape.healers = RAID_HEALERS_DEFAULT;
    return shape;
}

// The live guild's measured class distribution, with invented names and a
// level spread across the same band the real roster occupies.
//
// THE SEVEN WARRIORS ARE NAMED TO DISAGREE WITH THEMSELVES. Alphabetically
// they run Waramid, Warbex, Warcole, Wardin, Warelm, Warfen, Warlow; by level
// they run Warlow 47, Wardin 31, Warelm 30, Warbex 29, Warfen 29, Warcole 27,
// Waramid 20. The first four of each list share not one name, which is the
// whole point - see the header of this file.
std::vector<GuildMemberFacts> TheGuild()
{
    std::vector<GuildMemberFacts> members;

    members.push_back(Member("Waramid", WARRIOR, 20));
    members.push_back(Member("Warbex", WARRIOR, 29));
    members.push_back(Member("Warcole", WARRIOR, 27));
    members.push_back(Member("Wardin", WARRIOR, 31));
    members.push_back(Member("Warelm", WARRIOR, 30));
    members.push_back(Member("Warfen", WARRIOR, 29));
    members.push_back(Member("Warlow", WARRIOR, 47));

    members.push_back(Member("Priam", PRIEST, 17));
    members.push_back(Member("Pribeth", PRIEST, 32));
    members.push_back(Member("Pricasse", PRIEST, 36));
    members.push_back(Member("Pridove", PRIEST, 32));
    members.push_back(Member("Priessa", PRIEST, 32));
    members.push_back(Member("Prifane", PRIEST, 48));

    members.push_back(Member("Magaren", MAGE, 17));
    members.push_back(Member("Magbriar", MAGE, 33));
    members.push_back(Member("Magcinder", MAGE, 36));
    members.push_back(Member("Magdrift", MAGE, 35));
    members.push_back(Member("Magember", MAGE, 47));

    members.push_back(Member("Palandor", PALADIN, 18));
    members.push_back(Member("Palbrant", PALADIN, 16));
    members.push_back(Member("Palcorin", PALADIN, 37));
    members.push_back(Member("Paldrue", PALADIN, 45));

    members.push_back(Member("Hunarrow", HUNTER, 23));
    members.push_back(Member("Hunbram", HUNTER, 13));
    members.push_back(Member("Huncairn", HUNTER, 34));
    members.push_back(Member("Hundell", HUNTER, 38));

    members.push_back(Member("Rogalt", ROGUE, 30));
    members.push_back(Member("Rogbryn", ROGUE, 44));
    members.push_back(Member("Rogcade", ROGUE, 34));
    members.push_back(Member("Rogdusk", ROGUE, 37));

    members.push_back(Member("Locasha", WARLOCK, 18));
    members.push_back(Member("Locbane", WARLOCK, 21));
    members.push_back(Member("Loccrow", WARLOCK, 40));
    members.push_back(Member("Locdrek", WARLOCK, 29));

    members.push_back(Member("Druanni", DRUID, 26));
    members.push_back(Member("Drubarke", DRUID, 26));
    members.push_back(Member("Druchant", DRUID, 30));
    members.push_back(Member("Drudell", DRUID, 31));

    members.push_back(Member("Dekath", DEATH_KNIGHT, 55));
    members.push_back(Member("Shamund", SHAMAN, 29));

    return members;
}

// Look a seat up by member name. Returns false when the member was benched,
// which several tests below care about as an answer rather than as a failure.
bool SeatOf(RaidPlan const& plan, std::vector<GuildMemberFacts> const& members,
            std::string const& name, RaidSeatPlan& into)
{
    for (std::size_t i = 0; i < plan.seats.size(); ++i)
    {
        if (members[plan.seats[i].index].name != name)
            continue;
        into = plan.seats[i];
        return true;
    }
    return false;
}

void CountBySubgroup(RaidPlan const& plan, unsigned* held)
{
    for (unsigned g = 0; g < RAID_SUBGROUPS; ++g)
        held[g] = 0;
    for (std::size_t i = 0; i < plan.seats.size(); ++i)
        if (plan.seats[i].subgroup < RAID_SUBGROUPS)
            ++held[plan.seats[i].subgroup];
}

// ---------------------------------------------------------------------------

void TheGuildSeatsIntoEightFullGroupsWithNobodyLeftOut()
{
    std::vector<GuildMemberFacts> const members = TheGuild();
    CheckUnsigned("the fixture is a full raid", unsigned(members.size()), RAID_SIZE);

    RaidPlan const plan = PlanRaid(members, TheDefaultShape());

    CheckUnsigned("every member got a seat", unsigned(plan.seats.size()), RAID_SIZE);
    CheckUnsigned("and nobody is benched", unsigned(plan.benched.size()), 0);

    unsigned held[RAID_SUBGROUPS];
    CountBySubgroup(plan, held);
    for (unsigned g = 0; g < RAID_SUBGROUPS; ++g)
    {
        std::string const what = "group " + std::to_string(g + 1) + " holds five";
        CheckUnsigned(what.c_str(), held[g], RAID_SUBGROUP_SIZE);
    }

    // The two exclusive seats land on their quota exactly, and everybody else
    // is a damage seat. Asserted as a sum as well as individually, because the
    // failure this catches is a member seated twice or dropped between passes,
    // and either one leaves the individual counts looking plausible.
    CheckUnsigned("four tanks", plan.filled[unsigned(RaidSeat::Tank)], RAID_TANKS_DEFAULT);
    CheckUnsigned("ten healers", plan.filled[unsigned(RaidSeat::Healer)],
                  RAID_HEALERS_DEFAULT);
    unsigned total = 0;
    for (unsigned s = 0; s < RAID_SEAT_COUNT; ++s)
        total += plan.filled[s];
    CheckUnsigned("and the four seats account for the whole raid", total, RAID_SIZE);

    // No subgroup index may ever reach eight. The core indexes a heap array of
    // exactly eight bytes with this number and checks nothing.
    bool inRange = true;
    for (std::size_t i = 0; i < plan.seats.size(); ++i)
        inRange = inRange && plan.seats[i].subgroup < RAID_SUBGROUPS;
    Check("no seat names a subgroup the core cannot hold", inRange);
}

void TheTanksShareGroupOneAndGroupOneKeepsAHealer()
{
    std::vector<GuildMemberFacts> const members = TheGuild();
    RaidPlan const plan = PlanRaid(members, TheDefaultShape());

    unsigned tanksInGroupOne = 0;
    unsigned healersInGroupOne = 0;
    for (std::size_t i = 0; i < plan.seats.size(); ++i)
    {
        if (plan.seats[i].subgroup != 0)
        {
            Check("no tank is seated outside group 1",
                  plan.seats[i].seat != RaidSeat::Tank);
            continue;
        }
        if (plan.seats[i].seat == RaidSeat::Tank)
            ++tanksInGroupOne;
        if (plan.seats[i].seat == RaidSeat::Healer)
            ++healersInGroupOne;
    }
    CheckUnsigned("all four tanks are in group 1", tanksInGroupOne, RAID_TANKS_DEFAULT);
    CheckUnsigned("and a healer stands with them", healersInGroupOne, 1);

    // Every group gets a healer before any group gets a second. Ten healers
    // over eight groups means two groups hold two, which is the round going
    // back to the start - not eight groups with one and two piled somewhere.
    unsigned healers[RAID_SUBGROUPS] = {};
    for (std::size_t i = 0; i < plan.seats.size(); ++i)
        if (plan.seats[i].seat == RaidSeat::Healer)
            ++healers[plan.seats[i].subgroup];
    bool everyGroupHealed = true;
    for (unsigned g = 0; g < RAID_SUBGROUPS; ++g)
        everyGroupHealed = everyGroupHealed && healers[g] >= 1;
    Check("every one of the eight groups has a healer in it", everyGroupHealed);

    // And the healer standing with the tanks is the best one the roster has,
    // because group 1 is filled first and the healers are placed in the order
    // the picker chose them. Prifane is the level 48 priest.
    RaidSeatPlan best;
    Check("the top healer is seated", SeatOf(plan, members, "Prifane", best));
    Check("the top healer heals", best.seat == RaidSeat::Healer);
    CheckUnsigned("and stands with the tanks", best.subgroup, 0);
}

void TheTanksAreTheHighestLevelWarriorsAndNotTheFirstAlphabetically()
{
    std::vector<GuildMemberFacts> const members = TheGuild();
    RaidPlan const plan = PlanRaid(members, TheDefaultShape());

    // By level: Warlow 47, Wardin 31, Warelm 30, Warbex 29.
    char const* const tanking[] = {"Warlow", "Wardin", "Warelm", "Warbex"};
    // By name: Waramid, Warbex, Warcole, Wardin - three of which are NOT in the
    // list above. A comparator that fell back to name order fails here.
    char const* const notTanking[] = {"Waramid", "Warcole", "Warfen"};

    for (unsigned i = 0; i < 4; ++i)
    {
        RaidSeatPlan seat;
        std::string const what = std::string(tanking[i]) + " tanks";
        Check(what.c_str(), SeatOf(plan, members, tanking[i], seat)
                                && seat.seat == RaidSeat::Tank);
    }
    for (unsigned i = 0; i < 3; ++i)
    {
        RaidSeatPlan seat;
        std::string const what =
            std::string(notTanking[i]) + " is a lower level and does not tank";
        Check(what.c_str(), SeatOf(plan, members, notTanking[i], seat)
                                && seat.seat == RaidSeat::Melee);
    }
}

void TanksComeOffTheWarriorPileBeforeTheHealingClasses()
{
    // Seven warriors cover four tanks with three to spare, so no paladin and no
    // druid is ever taken out of the healing half of the roster. That is the
    // opportunity-cost argument the header makes, asserted rather than assumed.
    std::vector<GuildMemberFacts> const members = TheGuild();
    RaidPlan const plan = PlanRaid(members, TheDefaultShape());

    for (std::size_t i = 0; i < plan.seats.size(); ++i)
    {
        if (plan.seats[i].seat != RaidSeat::Tank)
            continue;
        CheckUnsigned("every tank is a warrior while warriors are spare",
                      members[plan.seats[i].index].classId, WARRIOR);
    }

    // And when they are NOT spare, the order carries on down the preference
    // list rather than giving up. Two warriors, one death knight, one druid,
    // one paladin - and the last three are all tank-capable, so the four tanks
    // are the two warriors, the death knight and the druid, in that order,
    // with the paladin left to heal.
    std::vector<GuildMemberFacts> thin;
    thin.push_back(Member("Waralpha", WARRIOR, 30));
    thin.push_back(Member("Warbeta", WARRIOR, 30));
    thin.push_back(Member("Dekgamma", DEATH_KNIGHT, 30));
    thin.push_back(Member("Drudelta", DRUID, 30));
    thin.push_back(Member("Palepsilon", PALADIN, 30));
    for (unsigned n = 0; n < 35; ++n)
        thin.push_back(Member(("Magfill" + std::to_string(100 + n)).c_str(), MAGE, 30));

    RaidPlan const second = PlanRaid(thin, TheDefaultShape());
    RaidSeatPlan seat;
    Check("the death knight tanks when the warriors run out",
          SeatOf(second, thin, "Dekgamma", seat) && seat.seat == RaidSeat::Tank);
    Check("and then the druid",
          SeatOf(second, thin, "Drudelta", seat) && seat.seat == RaidSeat::Tank);
    Check("the paladin is left to heal rather than tank",
          SeatOf(second, thin, "Palepsilon", seat) && seat.seat == RaidSeat::Healer);
}

void TheShamanFallsThroughToAMeleeGroupRatherThanHealing()
{
    // Six priests and four paladins fill the ten healer seats, so the roster's
    // one shaman is never asked to heal and lands in the melee half of the
    // group list, where its totems are spent on characters that swing.
    std::vector<GuildMemberFacts> const members = TheGuild();
    RaidPlan const plan = PlanRaid(members, TheDefaultShape());

    RaidSeatPlan shaman;
    Check("the shaman is seated", SeatOf(plan, members, "Shamund", shaman));
    Check("and is seated to fight in melee, not to heal",
          shaman.seat == RaidSeat::Melee);

    // The druids are not asked to heal either, for the same reason: the
    // priests and paladins got there first.
    RaidSeatPlan druid;
    Check("nor is the top druid asked to heal",
          SeatOf(plan, members, "Drudell", druid) && druid.seat != RaidSeat::Healer);
    Check("the druid stands at range", druid.seat == RaidSeat::Ranged);

    // The band table itself, asked directly, so a change to it is a change to
    // a test rather than a surprise in a layout.
    Check("a paladin who is not healing stands in melee",
          RaidDamageBand(PALADIN) == RaidSeat::Melee);
    Check("so does a shaman", RaidDamageBand(SHAMAN) == RaidSeat::Melee);
    Check("and a death knight", RaidDamageBand(DEATH_KNIGHT) == RaidSeat::Melee);
    Check("a druid stands at range", RaidDamageBand(DRUID) == RaidSeat::Ranged);
    Check("and so does a priest", RaidDamageBand(PRIEST) == RaidSeat::Ranged);
    Check("a rogue has nowhere else to be", RaidDamageBand(ROGUE) == RaidSeat::Melee);
    Check("and neither does a hunter", RaidDamageBand(HUNTER) == RaidSeat::Ranged);
}

// Where the highest melee and the lowest caster sit, which is the pair the
// opposite-ends rule is about.
void BandEdges(RaidPlan const& plan, unsigned& highestMelee, unsigned& lowestRanged,
               bool& anyMelee, bool& anyRanged)
{
    highestMelee = 0;
    lowestRanged = RAID_SUBGROUPS;
    anyMelee = false;
    anyRanged = false;
    for (std::size_t i = 0; i < plan.seats.size(); ++i)
    {
        unsigned const g = plan.seats[i].subgroup;
        if (plan.seats[i].seat == RaidSeat::Melee)
        {
            anyMelee = true;
            if (g > highestMelee)
                highestMelee = g;
        }
        if (plan.seats[i].seat == RaidSeat::Ranged)
        {
            anyRanged = true;
            if (g < lowestRanged)
                lowestRanged = g;
        }
    }
}

void MeleeAndCastersFillFromOppositeEndsOfTheGroupList()
{
    // A FULL FORTY CANNOT PROVE THIS ON ITS OWN, AND SAYING SO IS THE POINT OF
    // THE SECOND HALF OF THIS TEST. With forty characters there is no spare
    // seat anywhere: the melee pass fills the low groups solid, and the ranged
    // pass then has nowhere to go but the high ones whichever end it starts
    // from. Both directions produce the same layout, so an assertion against
    // the full raid passes for a version of this rule that does not exist. It
    // was written that way first and caught by changing `fill(Ranged, false)`
    // to `true` and watching every test still pass.
    std::vector<GuildMemberFacts> const members = TheGuild();
    RaidPlan const full = PlanRaid(members, TheDefaultShape());

    unsigned highestMelee = 0;
    unsigned lowestRanged = 0;
    bool anyMelee = false;
    bool anyRanged = false;
    BandEdges(full, highestMelee, lowestRanged, anyMelee, anyRanged);
    Check("the full fixture has melee", anyMelee);
    Check("the full fixture has ranged", anyRanged);
    // They may MEET in one group when the raid is exactly full - that is the
    // documented behaviour and one group is then mixed - but they may not
    // cross.
    Check("in a full raid melee never sit above the casters",
          highestMelee <= lowestRanged);

    // SO THE RULE IS PROVED ON A ROSTER WITH SLACK IN IT. Twenty characters is
    // two tanks and five healers by the scaling rule, which leaves five melee
    // and eight casters and three whole groups nobody has to sit in. Now the
    // direction of each pass is the only thing deciding where anybody goes.
    std::vector<GuildMemberFacts> twenty;
    twenty.push_back(Member("Waralpha", WARRIOR, 40));
    twenty.push_back(Member("Warbeta", WARRIOR, 39));
    twenty.push_back(Member("Wargamma", WARRIOR, 38));
    twenty.push_back(Member("Wardelta", WARRIOR, 37));
    twenty.push_back(Member("Warepsilon", WARRIOR, 36));
    twenty.push_back(Member("Prione", PRIEST, 35));
    twenty.push_back(Member("Pritwo", PRIEST, 34));
    twenty.push_back(Member("Prithree", PRIEST, 33));
    twenty.push_back(Member("Prifour", PRIEST, 32));
    twenty.push_back(Member("Prifive", PRIEST, 31));
    twenty.push_back(Member("Rogone", ROGUE, 30));
    twenty.push_back(Member("Rogtwo", ROGUE, 29));
    for (unsigned n = 0; n < 8; ++n)
        twenty.push_back(Member(("Magseat" + std::to_string(300 + n)).c_str(), MAGE,
                                28 - n));

    RaidPlan const slack = PlanRaid(twenty, TheDefaultShape());
    CheckUnsigned("the slack fixture seats everybody", unsigned(slack.seats.size()), 20);

    BandEdges(slack, highestMelee, lowestRanged, anyMelee, anyRanged);
    Check("the slack fixture has melee", anyMelee);
    Check("the slack fixture has ranged", anyRanged);
    // STRICTLY apart, with empty groups between them. This is the assertion the
    // full raid could not make.
    Check("with room to spare the casters sit strictly above the melee",
          highestMelee < lowestRanged);

    // And the whole occupancy pinned, because "apart" is the property and this
    // is the shape. Tanks and two melee fill group 1, three more melee sit in
    // group 2 behind the healer, one healer each holds groups 3 to 5, group 6
    // is empty, and the eight casters pack down from group 8.
    unsigned held[RAID_SUBGROUPS];
    CountBySubgroup(slack, held);
    unsigned const expected[RAID_SUBGROUPS] = {5, 4, 1, 1, 1, 0, 3, 5};
    for (unsigned g = 0; g < RAID_SUBGROUPS; ++g)
    {
        std::string const what =
            "twenty characters put " + std::to_string(expected[g])
            + " in group " + std::to_string(g + 1);
        CheckUnsigned(what.c_str(), held[g], expected[g]);
    }
}

void AGuildTooBigForARaidBenchesTheRestByNameRatherThanLosingThem()
{
    std::vector<GuildMemberFacts> members = TheGuild();
    for (unsigned n = 0; n < 5; ++n)
        members.push_back(Member(("Magspare" + std::to_string(200 + n)).c_str(), MAGE, 22));
    CheckUnsigned("the fixture is five over a raid", unsigned(members.size()),
                  RAID_SIZE + 5);

    RaidPlan const plan = PlanRaid(members, TheDefaultShape());

    CheckUnsigned("forty are seated", unsigned(plan.seats.size()), RAID_SIZE);
    CheckUnsigned("and five are named as standing outside it",
                  unsigned(plan.benched.size()), 5);
    CheckUnsigned("nobody vanished between the two lists",
                  unsigned(plan.seats.size() + plan.benched.size()),
                  unsigned(members.size()));

    // Every benched index is a real member and no index appears in both lists.
    bool seen[RAID_SIZE + 5] = {};
    bool twice = false;
    for (std::size_t i = 0; i < plan.seats.size(); ++i)
    {
        twice = twice || seen[plan.seats[i].index];
        seen[plan.seats[i].index] = true;
    }
    for (std::size_t i = 0; i < plan.benched.size(); ++i)
    {
        twice = twice || seen[plan.benched[i]];
        seen[plan.benched[i]] = true;
    }
    Check("nobody is both seated and benched", !twice);
}

void AShortRosterScalesItsQuotasInsteadOfDrowningInHealers()
{
    // The raw quotas are four tanks and ten healers. Run unscaled against a
    // twelve-member guild that is four tanks, eight healers and NOTHING that
    // kills anything - the failure this scaling exists to stop.
    std::vector<GuildMemberFacts> twelve;
    twelve.push_back(Member("Waralpha", WARRIOR, 40));
    twelve.push_back(Member("Warbeta", WARRIOR, 39));
    twelve.push_back(Member("Prigamma", PRIEST, 38));
    twelve.push_back(Member("Pridelta", PRIEST, 37));
    twelve.push_back(Member("Palepsilon", PALADIN, 36));
    twelve.push_back(Member("Druzeta", DRUID, 35));
    twelve.push_back(Member("Rogeta", ROGUE, 34));
    twelve.push_back(Member("Rogtheta", ROGUE, 33));
    twelve.push_back(Member("Magiota", MAGE, 32));
    twelve.push_back(Member("Magkappa", MAGE, 31));
    twelve.push_back(Member("Loclambda", WARLOCK, 30));
    twelve.push_back(Member("Hunmu", HUNTER, 29));

    RaidPlan const plan = PlanRaid(twelve, TheDefaultShape());

    CheckUnsigned("two tanks out of twelve", plan.filled[unsigned(RaidSeat::Tank)], 2);
    CheckUnsigned("three healers out of twelve",
                  plan.filled[unsigned(RaidSeat::Healer)], 3);
    unsigned const damage = plan.filled[unsigned(RaidSeat::Melee)]
                            + plan.filled[unsigned(RaidSeat::Ranged)];
    CheckUnsigned("and seven left to do the killing", damage, 7);
    CheckUnsigned("everybody is seated", unsigned(plan.seats.size()), 12);
    CheckUnsigned("and nobody is benched", unsigned(plan.benched.size()), 0);
}

void TheShapeRoundsUpAndNeverEatsTheWholeRoster()
{
    RaidShape const full = TheDefaultShape();

    RaidShape at40 = RaidShapeFor(full, RAID_SIZE);
    CheckUnsigned("a full raid gets the shape as written", at40.tanks, 4);
    CheckUnsigned("healers too", at40.healers, 10);

    RaidShape at12 = RaidShapeFor(full, 12);
    CheckUnsigned("twelve members want two tanks", at12.tanks, 2);
    CheckUnsigned("and three healers", at12.healers, 3);

    RaidShape at5 = RaidShapeFor(full, 5);
    CheckUnsigned("a party of five wants one tank", at5.tanks, 1);
    CheckUnsigned("and two healers", at5.healers, 2);

    // The two edges. Neither can be reached through the guild verb - it refuses
    // an empty roster before it plans - but the arithmetic has to be right
    // anyway, because an underflow in `members - shape.tanks` would produce an
    // enormous healer quota rather than a small one.
    RaidShape at1 = RaidShapeFor(full, 1);
    CheckUnsigned("a single member is a tank", at1.tanks, 1);
    CheckUnsigned("and there is nobody left to heal", at1.healers, 0);

    RaidShape at0 = RaidShapeFor(full, 0);
    CheckUnsigned("an empty roster wants no tanks", at0.tanks, 0);
    CheckUnsigned("and no healers", at0.healers, 0);

    // A caller that asked for more exclusive seats than there are members gets
    // a shape that still leaves the roster intact rather than one that wraps.
    RaidShape greedy;
    greedy.tanks = 40;
    greedy.healers = 40;
    RaidShape const squeezed = RaidShapeFor(greedy, 6);
    Check("an over-asking shape never wants more than the roster holds",
          squeezed.tanks + squeezed.healers <= 6);
}

void ASeatSaysWhatItIsInWordsAPersonCanRead()
{
    std::vector<GuildMemberFacts> const members = TheGuild();
    RaidPlan const plan = PlanRaid(members, TheDefaultShape());

    RaidSeatPlan tank;
    Check("the top warrior is seated", SeatOf(plan, members, "Warlow", tank));
    // GROUPS ARE PRINTED ONE-BASED AND STORED ZERO-BASED. The owner asked for
    // "group 1 thru 8" and `group_member`.`subgroup` holds 0 to 7; this is the
    // one place the two meet, so it is pinned.
    CheckUnsigned("and stored zero-based", tank.subgroup, 0);
    CheckString("but said one-based", tank.said, "Tank in group 1");

    CheckString("the seat names read as words", RaidSeatName(RaidSeat::Healer),
                "Healer");
    CheckString("all four of them", RaidSeatName(RaidSeat::Ranged), "Ranged");

    bool everySeatSpeaks = true;
    for (std::size_t i = 0; i < plan.seats.size(); ++i)
        everySeatSpeaks = everySeatSpeaks && !plan.seats[i].said.empty();
    Check("no seat is handed out without a reason", everySeatSpeaks);
}

// ---------------------------------------------------------------------------
// The moves.

// Apply a move list the way the adapter applies it to a Group, and answer where
// everybody ended up.
std::vector<unsigned> AfterTheMoves(std::vector<RaidSeatNow> const& seats,
                                    std::vector<RaidMove> const& moves)
{
    std::vector<unsigned> at(seats.size(), 0);
    for (std::size_t i = 0; i < seats.size(); ++i)
        at[i] = seats[i].subgroup;
    for (std::size_t m = 0; m < moves.size(); ++m)
        at[moves[m].who] = moves[m].to;
    return at;
}

void TheMovesTurnAFreshlyFilledRaidIntoThePlannedOne()
{
    // WHERE THE CORE PUTS PEOPLE ON ITS OWN, which is the state this has to
    // start from. Group::AddMember walks the subgroup counters and takes the
    // first one under five, so forty characters added in roster order land as
    // five in group 1, five in group 2, and so on - the arrival order, not the
    // plan.
    std::vector<GuildMemberFacts> const members = TheGuild();
    RaidPlan const plan = PlanRaid(members, TheDefaultShape());

    std::vector<RaidSeatNow> seats(members.size());
    for (std::size_t i = 0; i < members.size(); ++i)
    {
        seats[i].subgroup = unsigned(i) / RAID_SUBGROUP_SIZE;
        seats[i].planned = false;
    }
    for (std::size_t i = 0; i < plan.seats.size(); ++i)
    {
        seats[plan.seats[i].index].want = plan.seats[i].subgroup;
        seats[plan.seats[i].index].planned = true;
    }

    std::vector<RaidMove> const moves = RaidSeatingMoves(seats);

    // Every move is legal on its face.
    bool inRange = true;
    for (std::size_t m = 0; m < moves.size(); ++m)
        inRange = inRange && moves[m].to < RAID_SUBGROUPS
                  && moves[m].who < seats.size();
    Check("no move names a subgroup or a member that does not exist", inRange);

    // At most one swap - two moves - per member, which is the bound the
    // termination argument gives. A loop that cycled would blow through this
    // long before it hung anything.
    Check("the move list is bounded by two per member",
          moves.size() <= 2 * seats.size());

    // AND THE POINT: apply them and compare. The raid must actually be the
    // planned raid afterwards, not merely have been shuffled.
    std::vector<unsigned> const at = AfterTheMoves(seats, moves);
    bool arrived = true;
    for (std::size_t i = 0; i < seats.size(); ++i)
        if (seats[i].planned)
            arrived = arrived && at[i] == seats[i].want;
    Check("every planned member ends up in the group the plan gave them", arrived);

    unsigned held[RAID_SUBGROUPS] = {};
    for (std::size_t i = 0; i < at.size(); ++i)
        ++held[at[i]];
    bool legal = true;
    for (unsigned g = 0; g < RAID_SUBGROUPS; ++g)
        legal = legal && held[g] <= RAID_SUBGROUP_SIZE;
    Check("and no subgroup is left holding more than five", legal);

    // The fixture has to actually need moving, or everything above passes
    // vacuously against an empty move list.
    Check("the arrival order was not already the plan", !moves.empty());
}

void AFullDestinationIsASwapAndNotAnOverfill()
{
    // Two groups of five that want to be each other. Every single move here has
    // to be a swap, because neither destination ever has a free seat.
    std::vector<RaidSeatNow> seats(10);
    for (std::size_t i = 0; i < 10; ++i)
    {
        seats[i].subgroup = i < 5 ? 0u : 1u;
        seats[i].want = i < 5 ? 1u : 0u;
        seats[i].planned = true;
    }

    std::vector<RaidMove> const moves = RaidSeatingMoves(seats);
    Check("the swap emits something", !moves.empty());

    std::vector<unsigned> const at = AfterTheMoves(seats, moves);
    bool arrived = true;
    for (std::size_t i = 0; i < 10; ++i)
        arrived = arrived && at[i] == seats[i].want;
    Check("two full groups can trade places", arrived);

    unsigned held[RAID_SUBGROUPS] = {};
    for (std::size_t i = 0; i < at.size(); ++i)
        ++held[at[i]];
    CheckUnsigned("group 1 still holds five afterwards", held[0], 5);
    CheckUnsigned("and so does group 2", held[1], 5);
}

void SomebodyThePlanNeverNamedIsDisplacedRatherThanLeftInTheWay()
{
    // Five characters nobody planned for are sitting in group 1, and one
    // planned character in group 2 wants in. The group is full, so one of the
    // five has to move - and the one that moves goes to the group the planned
    // member vacated, never somewhere invented.
    std::vector<RaidSeatNow> seats(6);
    for (std::size_t i = 0; i < 5; ++i)
    {
        seats[i].subgroup = 0;
        seats[i].planned = false;
    }
    seats[5].subgroup = 1;
    seats[5].want = 0;
    seats[5].planned = true;

    std::vector<RaidMove> const moves = RaidSeatingMoves(seats);
    std::vector<unsigned> const at = AfterTheMoves(seats, moves);

    CheckUnsigned("the planned member got the seat it was given", at[5], 0);

    unsigned displaced = 0;
    for (std::size_t i = 0; i < 5; ++i)
        if (at[i] != 0)
        {
            ++displaced;
            CheckUnsigned("and the one it displaced took the seat it left", at[i], 1);
        }
    CheckUnsigned("exactly one unplanned member was displaced", displaced, 1);
}

void ACharacterTheCoreDoesNotHaveIsNeverMoved()
{
    // Group::GetMemberGroup answers MAX_RAID_SUBGROUPS + 1 for a guid it cannot
    // find. A move emitted for one of those would be a no-op in the core and a
    // seat this function had already counted as taken - so the next member who
    // wanted that group would be pushed into a group holding six.
    std::vector<RaidSeatNow> seats(2);
    seats[0].subgroup = RAID_SUBGROUPS + 1;   // not in this raid at all
    seats[0].want = 0;
    seats[0].planned = true;
    seats[1].subgroup = 1;
    seats[1].want = 0;
    seats[1].planned = true;

    std::vector<RaidMove> const moves = RaidSeatingMoves(seats);

    bool touchedTheStranger = false;
    for (std::size_t m = 0; m < moves.size(); ++m)
        touchedTheStranger = touchedTheStranger || moves[m].who == 0;
    Check("a character the core does not have is never moved", !touchedTheStranger);

    std::vector<unsigned> const at = AfterTheMoves(seats, moves);
    CheckUnsigned("and the member who IS in the raid still gets moved", at[1], 0);
}

void AlreadySeatedIsNoMovesAtAll()
{
    std::vector<RaidSeatNow> seats(10);
    for (std::size_t i = 0; i < 10; ++i)
    {
        seats[i].subgroup = unsigned(i) / RAID_SUBGROUP_SIZE;
        seats[i].want = seats[i].subgroup;
        seats[i].planned = true;
    }
    std::vector<RaidMove> const moves = RaidSeatingMoves(seats);
    CheckUnsigned("a raid already in its plan is moved not at all",
                  unsigned(moves.size()), 0);
}

void TheRaidRowParses()
{
    GuildRequest const read = ParseGuildRequest("raid");
    Check("a bare raid row reads the plan", read.verb == GuildVerb::Raid);
    CheckString("and is refused by nothing", read.error, "");

    GuildRequest const act = ParseGuildRequest("raid form");
    Check("raid form is the verb that acts", act.verb == GuildVerb::RaidForm);

    // THE VERB IS LOWERCASED AND THE WORD AFTER IT IS NOT, which is not a
    // decision this verb made - it is the parser's shape and `bank
    // deposit-item` already relies on it (tests/test_guild.cpp asserts `BANK
    // deposit-item` parses and says nothing about `DEPOSIT-ITEM`). Pinned here
    // so that a future tidy-up which lowercased the whole row would be a
    // visible change to a test rather than a quiet widening of two grammars.
    Check("the verb is case insensitive like the others",
          ParseGuildRequest("RAID form").verb == GuildVerb::RaidForm);
    Check("but the word after it is not",
          ParseGuildRequest("raid FORM").verb == GuildVerb::None);
    Check("blanks do not matter",
          ParseGuildRequest("  raid   form  ").verb == GuildVerb::RaidForm);

    // A MISSPELLING MUST NOT FALL BACK TO THE SAFE HALF. `raid frm` reading the
    // plan would be a grammar in which a typo silently changes which of two
    // verbs ran, and the other direction of that same grammar converts a group
    // to a raid for ever.
    CheckString("a word raid does not know is refused rather than ignored",
                ParseGuildRequest("raid frm").error,
                OverseerDecisions::GuildRefusal::RaidTakesFormOrNothing);
    Check("and refuses rather than reading", ParseGuildRequest("raid frm").verb
                                                 == GuildVerb::None);
    CheckString("so is a trailing word after form",
                ParseGuildRequest("raid form now").error,
                OverseerDecisions::GuildRefusal::RaidTakesFormOrNothing);
}

}  // namespace

int main()
{
    TheGuildSeatsIntoEightFullGroupsWithNobodyLeftOut();
    TheTanksShareGroupOneAndGroupOneKeepsAHealer();
    TheTanksAreTheHighestLevelWarriorsAndNotTheFirstAlphabetically();
    TanksComeOffTheWarriorPileBeforeTheHealingClasses();
    TheShamanFallsThroughToAMeleeGroupRatherThanHealing();
    MeleeAndCastersFillFromOppositeEndsOfTheGroupList();
    AGuildTooBigForARaidBenchesTheRestByNameRatherThanLosingThem();
    AShortRosterScalesItsQuotasInsteadOfDrowningInHealers();
    TheShapeRoundsUpAndNeverEatsTheWholeRoster();
    ASeatSaysWhatItIsInWordsAPersonCanRead();
    TheMovesTurnAFreshlyFilledRaidIntoThePlannedOne();
    AFullDestinationIsASwapAndNotAnOverfill();
    SomebodyThePlanNeverNamedIsDisplacedRatherThanLeftInTheWay();
    ACharacterTheCoreDoesNotHaveIsNeverMoved();
    AlreadySeatedIsNoMovesAtAll();
    TheRaidRowParses();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the raid decisions hold\n");
    return EXIT_SUCCESS;
}
