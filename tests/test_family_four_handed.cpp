/*
 * A family member sits out with no guest, and the family runs four-handed
 * (operator decision, 2026-10-05): when no guild member can take the seat of
 * a member ordered out to craft, the bridge writes the stand-in row with an
 * empty `in_name`. The family's run party is then the roster without that
 * member, the dungeon finder (which takes only a full five) refuses it, and
 * the run goes in by the walk-in path, whose barrier and doorstep read the run
 * party rather than the roster.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::ApproachLimits;
using OverseerDecisions::ChooseStandins;
using OverseerDecisions::CheckStandin;
using OverseerDecisions::DungeonRunBarrierMet;
using OverseerDecisions::DungeonRunEntryReady;
using OverseerDecisions::DungeonRunEntryState;
using OverseerDecisions::DungeonRunMemberState;
using OverseerDecisions::FamilyMember;
using OverseerDecisions::FamilyRoster;
using OverseerDecisions::FamilyStandin;
using OverseerDecisions::FinderFacts;
using OverseerDecisions::FinderIsTheWayIn;
using OverseerDecisions::FinderMember;
using OverseerDecisions::NextFrozenStandin;
using OverseerDecisions::ReadFinderReadiness;
using OverseerDecisions::RunParty;
using OverseerDecisions::StandinBook;
using OverseerDecisions::StandinGuestWord;
using OverseerDecisions::StandinMove;
using OverseerDecisions::StandinRefusal;
using OverseerDecisions::StandinSeat;
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
std::vector<std::string> const kFourHanded{"Grug", "Ugga", "Grog", "Bork"};
constexpr ApproachLimits LIMITS{10.f, 60.f, 20.f};

FamilyStandin SitOut(std::string const& out, std::string const& family = "Grug")
{
    FamilyStandin row;
    row.family = family;
    row.outName = out;
    row.inName = "";
    row.seat = StandinSeat::Damage;
    row.reason = "no guest at scarlet-library; the family runs four-handed";
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

void ARowWithNoGuestIsLegal()
{
    FamilyStandin const row = SitOut("Og");
    Check("a row with no guest is a stand-in", row.Active());
    Check("and seats nobody", !row.HasGuest());
    Check("its guest reads as nobody", StandinGuestWord(row) == "nobody");
    Check("a row with no guest passes the check",
          CheckStandin(kFamily, "Grug", row, kFamily) == StandinRefusal::None);
    Check("the head still never sits out",
          CheckStandin(kFamily, "Grug", SitOut("Grug"), kFamily) ==
              StandinRefusal::SwapsTheHead);
    FamilyStandin noOut = SitOut("");
    Check("a row with nobody sitting out is incomplete",
          CheckStandin(kFamily, "Grug", noOut, kFamily) == StandinRefusal::Incomplete);
}

void TheRunPartyIsTheFourLeft()
{
    FamilyStandin const row = SitOut("Og");
    std::vector<std::string> const party = RunParty(kFamily, "Grug", &row);
    Check("the run party is four", party.size() == 4);
    Check("the four left, in roster order", party == kFourHanded);
    FamilyStandin const head = SitOut("Grug");
    Check("a row sitting the head out leaves the family whole",
          RunParty(kFamily, "Grug", &head) == kFamily);
}

void TwoFamiliesMayBothRunShortHanded()
{
    std::vector<FamilyRoster> const rosters{
        Roster("horde", {"Grug", "Og", "Ugga", "Grog", "Bork"}),
        Roster("alliance", {"Ann", "Bea", "Cal", "Dee", "Eve"}),
    };
    StandinBook const book = ChooseStandins({SitOut("Og"), SitOut("Bea", "Ann")}, rosters);
    Check("both rows with no guest are applied", book.byHead.size() == 2 && book.refused.empty());
}

void NoGuestIsWaitedFor()
{
    FamilyStandin const none;
    FamilyStandin const row = SitOut("Og");
    StandinStep step = NextFrozenStandin(StandinWindow::Idle, none, row, false);
    Check("IDLE applies a row with no guest without waiting for one",
          step.move == StandinMove::Apply && step.frozen.SameSwap(row));
    step = NextFrozenStandin(StandinWindow::Running, none, row, false);
    Check("never mid-run", step.move == StandinMove::Keep && !step.frozen.Active());
    step = NextFrozenStandin(StandinWindow::Running, row, none, false);
    Check("a row deleted mid-run keeps the four-handed party",
          step.move == StandinMove::Keep && step.frozen.SameSwap(row));
    step = NextFrozenStandin(StandinWindow::Idle, row, none, false);
    Check("the row gone between runs brings Og back",
          step.move == StandinMove::Release && !step.frozen.Active());

    FamilyStandin guest = row;
    guest.inName = "Thak";
    step = NextFrozenStandin(StandinWindow::Resetting, row, guest, true);
    Check("a guest found later takes the empty seat between runs",
          step.move == StandinMove::Swap && step.frozen.SameSwap(guest));
}

// THE WALK-IN READINESS OF THE FOUR: the finder refuses a party under five, so
// the run is not sent to the queue, and the walk-in barrier and doorstep read
// the four, so Og off crafting elsewhere holds nothing.
void TheFourGoInByTheWalkIn()
{
    FamilyStandin const row = SitOut("Og");
    std::vector<std::string> const party = RunParty(kFamily, "Grug", &row);

    FinderFacts facts;
    facts.enabled = true;
    facts.finderOn = true;
    facts.dungeonId = 1;
    facts.groupExists = true;
    facts.headLeads = true;
    facts.groupSize = static_cast<unsigned>(party.size());
    unsigned const classes[] = {1, 5, 8, 4, 3};
    for (std::size_t i = 0; i < party.size(); ++i)
    {
        FinderMember m;
        m.name = party[i];
        m.leader = i == 0;
        m.inWorld = m.alive = m.inHeadsGroup = true;
        m.classId = classes[i];
        facts.family.push_back(m);
    }
    bool const finderReady = ReadFinderReadiness(facts, false).ready;
    Check("the finder refuses the four", !finderReady);
    Check("so the finder is not the way in, even where it is the realm's default",
          !FinderIsTheWayIn(true, finderReady, 1, 0));

    std::vector<DungeonRunMemberState> staged;
    std::vector<DungeonRunEntryState> doorstep;
    for (std::string const& name : party)
    {
        DungeonRunMemberState m;
        m.name = name;
        // Og, crafting in town, is nowhere near the stage or the door.
        bool const here = name != "Og";
        m.seen = here;
        m.alive = true;
        m.distanceFromStage = here ? 3.f : -1.f;
        staged.push_back(m);
        DungeonRunEntryState e;
        e.name = name;
        e.seen = here;
        e.alive = true;
        e.distanceFromDoor = here ? 2.f : -1.f;
        doorstep.push_back(e);
    }
    Check("the staging barrier is met by the four", DungeonRunBarrierMet(staged, LIMITS));
    Check("and the doorstep is ready for them", DungeonRunEntryReady(doorstep, 5.f));
}

}  // namespace

int main()
{
    ARowWithNoGuestIsLegal();
    TheRunPartyIsTheFourLeft();
    TwoFamiliesMayBothRunShortHanded();
    NoGuestIsWaitedFor();
    TheFourGoInByTheWalkIn();
    if (failures)
        return 1;
    std::printf("ok test_family_four_handed\n");
    return 0;
}
