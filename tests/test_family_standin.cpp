/*
 * A family member sits out and a guild member stands in (operator request,
 * 2026-10-05): when a family member is busy tailoring, a different damage
 * dealer, tank or healer takes the seat and the family's dungeon campaign keeps
 * running.
 *
 * The bridge writes one overseer_family_standin row per family; the module
 * reads it, checks it, builds the run's party from it, and applies it only
 * between runs, freezing it for the whole of a run.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::ChooseStandins;
using OverseerDecisions::CheckStandin;
using OverseerDecisions::FamilyMember;
using OverseerDecisions::FamilyRoster;
using OverseerDecisions::FamilyStandin;
using OverseerDecisions::FINDER_GROUP_SIZE;
using OverseerDecisions::FINDER_ROLE_DAMAGE;
using OverseerDecisions::FINDER_ROLE_HEALER;
using OverseerDecisions::FINDER_ROLE_TANK;
using OverseerDecisions::FinderFacts;
using OverseerDecisions::FinderMember;
using OverseerDecisions::FinderMemberRoleMask;
using OverseerDecisions::NextFrozenStandin;
using OverseerDecisions::ParseStandinSeat;
using OverseerDecisions::ReadFinderReadiness;
using OverseerDecisions::RunParty;
using OverseerDecisions::StandinBook;
using OverseerDecisions::StandinGuestIsSteerable;
using OverseerDecisions::StandinMove;
using OverseerDecisions::StandinRefusal;
using OverseerDecisions::StandinSeat;
using OverseerDecisions::StandinSeatRoleMask;
using OverseerDecisions::StandinStep;
using OverseerDecisions::StandinWindow;

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

std::vector<std::string> const kFamily{"Grug", "Og", "Ugga", "Grog", "Bork"};

FamilyStandin Row(std::string const& out, std::string const& in,
                  StandinSeat seat = StandinSeat::Damage, std::string const& family = "Grug")
{
    FamilyStandin row;
    row.family = family;
    row.outName = out;
    row.inName = in;
    row.seat = seat;
    row.reason = "tailoring";
    return row;
}

FamilyRoster Roster(std::string const& family, std::vector<std::string> const& names)
{
    FamilyRoster roster;
    roster.family = family;
    roster.leader = names.front();
    for (std::string const& name : names)
        roster.members.push_back(FamilyMember{name, family, name == names.front()});
    return roster;
}

void TheRunPartySwapsOneSeat()
{
    FamilyStandin const row = Row("Og", "Thak");
    std::vector<std::string> const party = RunParty(kFamily, "Grug", &row);
    Check("the run party is still five", party.size() == 5);
    Check("the guest takes the out member's place in roster order",
          party == std::vector<std::string>({"Grug", "Thak", "Ugga", "Grog", "Bork"}));
    Check("no stand-in is the roster unchanged", RunParty(kFamily, "Grug", nullptr) == kFamily);
    FamilyStandin const none;
    Check("an inactive stand-in is the roster unchanged", RunParty(kFamily, "Grug", &none) == kFamily);
}

void TheHeadNeverSitsOut()
{
    FamilyStandin const row = Row("Grug", "Thak");
    Check("swapping the head is refused",
          CheckStandin(kFamily, "Grug", row, kFamily) == StandinRefusal::SwapsTheHead);
    Check("and the run party keeps the head", RunParty(kFamily, "Grug", &row) == kFamily);
}

void TheGuestIsNotOnTheRoster()
{
    FamilyStandin const own = Row("Og", "Ugga");
    Check("a guest from the same family is refused",
          CheckStandin(kFamily, "Grug", own, kFamily) == StandinRefusal::GuestOnRoster);
    Check("and never doubles a member", RunParty(kFamily, "Grug", &own) == kFamily);

    std::vector<std::string> whole = kFamily;
    whole.push_back("Thak");
    FamilyStandin const other = Row("Og", "Thak");
    Check("a guest from another family is refused",
          CheckStandin(kFamily, "Grug", other, whole) == StandinRefusal::GuestOnRoster);
}

void OtherBadRowsAreRefused()
{
    Check("an out member from outside the family is refused",
          CheckStandin(kFamily, "Grug", Row("Zug", "Thak"), kFamily) ==
              StandinRefusal::OutNotInFamily);
    Check("the same character on both sides is refused",
          CheckStandin(kFamily, "Grug", Row("Og", "Og"), kFamily) == StandinRefusal::GuestIsOut);
    Check("a row for another head is refused",
          CheckStandin(kFamily, "Grug", Row("Og", "Thak", StandinSeat::Damage, "Zug"), kFamily) ==
              StandinRefusal::NoSuchFamily);
    Check("an empty member sitting out is refused",
          CheckStandin(kFamily, "Grug", Row("", "Thak"), kFamily) == StandinRefusal::Incomplete);
    Check("an empty guest is a sit-out with no guest (test_family_four_handed)",
          CheckStandin(kFamily, "Grug", Row("Og", ""), kFamily) == StandinRefusal::None);
    Check("a legal row passes",
          CheckStandin(kFamily, "Grug", Row("Og", "Thak"), kFamily) == StandinRefusal::None);
}

void OneStandinPerFamilyAndOneFamilyPerGuest()
{
    std::vector<FamilyRoster> const rosters{
        Roster("horde", {"Grug", "Og", "Ugga", "Grog", "Bork"}),
        Roster("alliance", {"Ann", "Bea", "Cal", "Dee", "Eve"}),
    };
    std::vector<FamilyStandin> const rows{
        Row("Og", "Thak"),
        Row("Ugga", "Mok"),                                   // second for Grug
        Row("Bea", "Thak", StandinSeat::Healer, "Ann"),       // Thak is taken
        Row("Cal", "Fen", StandinSeat::Tank, "Ann"),
        Row("Og", "Thak", StandinSeat::Damage, "Nobody"),     // no such head
    };
    StandinBook const book = ChooseStandins(rows, rosters);
    Check("two families each get one", book.byHead.size() == 2);
    Check("the first row in id order wins for Grug",
          book.byHead.count("Grug") && book.byHead.at("Grug").inName == "Thak");
    Check("Ann gets the row whose guest is free",
          book.byHead.count("Ann") && book.byHead.at("Ann").inName == "Fen");
    Check("three rows are refused", book.refused.size() == 3);
    Check("the second row for a family is refused for that reason",
          book.refused.size() == 3 && book.refused[0].why == StandinRefusal::SecondForFamily);
    Check("a guest asked for twice is refused the second time",
          book.refused.size() == 3 && book.refused[1].why == StandinRefusal::GuestTaken);
    Check("a row for a head with no family is refused",
          book.refused.size() == 3 && book.refused[2].why == StandinRefusal::NoSuchFamily);
}

void AppliedBetweenRunsOnly()
{
    FamilyStandin const none;
    FamilyStandin const row = Row("Og", "Thak");

    StandinStep step = NextFrozenStandin(StandinWindow::Idle, none, row, true);
    Check("IDLE applies a row", step.move == StandinMove::Apply && step.frozen.SameSwap(row));

    step = NextFrozenStandin(StandinWindow::Resetting, none, row, true);
    Check("RESETTING applies a row", step.move == StandinMove::Apply && step.frozen.SameSwap(row));

    step = NextFrozenStandin(StandinWindow::Running, none, row, true);
    Check("a running run does not take a guest mid-run",
          step.move == StandinMove::Keep && !step.frozen.Active());

    step = NextFrozenStandin(StandinWindow::Idle, none, row, false);
    Check("a guest who is not in the world is waited for",
          step.move == StandinMove::WaitForGuest && !step.frozen.Active());
}

void FrozenForTheWholeRun()
{
    FamilyStandin const none;
    FamilyStandin const row = Row("Og", "Thak");
    FamilyStandin const other = Row("Ugga", "Mok", StandinSeat::Healer);

    StandinStep step = NextFrozenStandin(StandinWindow::Running, row, none, true);
    Check("a row deleted mid-run keeps the frozen party",
          step.move == StandinMove::Keep && step.frozen.SameSwap(row));

    step = NextFrozenStandin(StandinWindow::Running, row, other, true);
    Check("a row changed mid-run keeps the frozen party",
          step.move == StandinMove::Keep && step.frozen.SameSwap(row));

    step = NextFrozenStandin(StandinWindow::Idle, row, none, true);
    Check("IDLE with the row gone releases it",
          step.move == StandinMove::Release && !step.frozen.Active());

    step = NextFrozenStandin(StandinWindow::Idle, row, other, true);
    Check("IDLE with a different row swaps it",
          step.move == StandinMove::Swap && step.frozen.SameSwap(other));

    FamilyStandin reworded = row;
    reworded.reason = "still tailoring";
    step = NextFrozenStandin(StandinWindow::Idle, row, reworded, true);
    Check("new words for the same swap change nothing", step.move == StandinMove::Keep);
}

// THE BEHAVIOUR, END TO END: a campaign's poll sequence, with the table read
// at every poll and the run's party built from what is frozen.
void ACampaignRunsWithTheGuest()
{
    FamilyStandin frozen;
    FamilyStandin const row = Row("Og", "Thak", StandinSeat::Healer);
    FamilyStandin const none;
    auto poll = [&frozen](StandinWindow window, FamilyStandin const& table) {
        frozen = NextFrozenStandin(window, frozen, table, true).frozen;
        return RunParty(kFamily, "Grug", &frozen);
    };

    std::vector<std::string> const withGuest{"Grug", "Thak", "Ugga", "Grog", "Bork"};
    Check("the row lands at IDLE and the party takes the guest",
          poll(StandinWindow::Idle, row) == withGuest);
    Check("the run carries the guest through GATHERING",
          poll(StandinWindow::Running, row) == withGuest);
    Check("the bridge deletes the row mid-run and the guest stays in",
          poll(StandinWindow::Running, none) == withGuest);
    Check("and stays in to the end of the run",
          poll(StandinWindow::Running, none) == withGuest);
    Check("the run ends at IDLE and the family is whole again",
          poll(StandinWindow::Idle, none) == kFamily);
}

void TheGuestAnswersWithItsSeat()
{
    Check("a tank guest is a tank and not a leader",
          StandinSeatRoleMask(StandinSeat::Tank) == FINDER_ROLE_TANK);
    Check("a healer guest heals", StandinSeatRoleMask(StandinSeat::Healer) == FINDER_ROLE_HEALER);
    Check("a dps guest deals damage",
          StandinSeatRoleMask(StandinSeat::Damage) == FINDER_ROLE_DAMAGE);

    StandinSeat seat = StandinSeat::Tank;
    Check("'dps' parses", ParseStandinSeat("dps", seat) && seat == StandinSeat::Damage);
    Check("'healer' parses", ParseStandinSeat("healer", seat) && seat == StandinSeat::Healer);
    Check("'tank' parses", ParseStandinSeat("tank", seat) && seat == StandinSeat::Tank);
    Check("anything else does not", !ParseStandinSeat("damage", seat));

    FinderMember guest;
    guest.classId = 1;   // a warrior could tank or deal damage
    guest.roleMask = StandinSeatRoleMask(StandinSeat::Damage);
    Check("a guest's seat mask overrides its class",
          FinderMemberRoleMask(guest) == FINDER_ROLE_DAMAGE);
    FinderMember member;
    member.classId = 1;
    Check("a family member answers with its class as before",
          FinderMemberRoleMask(member) ==
              OverseerDecisions::FinderRoleMask(member.classId, member.leader));
}

// The finder's readiness reads the guest's seat: a family that sat its healer
// out for a healer guest of a class the readiness would not count as one is
// ready, because the guest answers the role check with its seat.
void TheFinderCountsTheGuestsSeat()
{
    FinderFacts facts;
    facts.enabled = true;
    facts.finderOn = true;
    facts.dungeonId = 1;
    facts.groupExists = true;
    facts.groupSize = FINDER_GROUP_SIZE;
    facts.headLeads = true;
    // Warrior head, then rogue, mage, warrior and a guest.
    unsigned const classes[] = {1, 4, 8, 1, 4};
    for (unsigned i = 0; i < 5; ++i)
    {
        FinderMember m;
        m.name = "m" + std::to_string(i);
        m.leader = i == 0;
        m.inWorld = m.alive = m.inHeadsGroup = true;
        m.classId = classes[i];
        facts.family.push_back(m);
    }
    Check("with no healer class the family is not ready", !ReadFinderReadiness(facts, true).ready);
    facts.family[4].roleMask = StandinSeatRoleMask(StandinSeat::Healer);
    Check("a guest seated as healer makes it ready", ReadFinderReadiness(facts, true).ready);
}

void TheGuestIsSteerableOnlyWhenRegistered()
{
    Check("a registered guest bot in the world is steerable",
          StandinGuestIsSteerable(true, false, true, true));
    Check("a registered guest with a client is steerable",
          StandinGuestIsSteerable(true, true, true, false));
    Check("an unregistered bot is not", !StandinGuestIsSteerable(false, false, true, true));
    Check("a registered guest out of the world is not",
          !StandinGuestIsSteerable(true, false, false, true));
}

}  // namespace

int main()
{
    TheRunPartySwapsOneSeat();
    TheHeadNeverSitsOut();
    TheGuestIsNotOnTheRoster();
    OtherBadRowsAreRefused();
    OneStandinPerFamilyAndOneFamilyPerGuest();
    AppliedBetweenRunsOnly();
    FrozenForTheWholeRun();
    ACampaignRunsWithTheGuest();
    TheGuestAnswersWithItsSeat();
    TheFinderCountsTheGuestsSeat();
    TheGuestIsSteerableOnlyWhenRegistered();
    if (failures)
        return 1;
    std::printf("ok test_family_standin\n");
    return 0;
}
