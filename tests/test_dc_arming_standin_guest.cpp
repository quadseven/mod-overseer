/*
 * The arming drive issues `dc on` to a family's stand-in guest (2026-11-05).
 *
 * The CLEARING census counts the run's party, guest included, and holds its
 * stall clock until `dc on` is accepted on everyone inside. The drive read the
 * roster table alone, so the guest was never armed, the census read "4 of 5",
 * and the clock that drives the `dc skip`, re-arm and exit ladder stayed held.
 * Measured on the dev realm: a family sat in Maraudon for about 57,000 seconds
 * with the dungeon brain stopped, until the guest logged out.
 *
 * Two halves: the pure decision (who the drive arms), and the wiring in the
 * adapter (the drive loops over that decision, and a guest joins its family's
 * run instead of opening one).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using OverseerDecisions::DcArmingSubject;
using OverseerDecisions::DcArmingSubjects;
using OverseerDecisions::FamilyStandin;
using OverseerDecisions::StandinSeat;

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

FamilyStandin Row(std::string const& head, std::string const& out, std::string const& in,
                  StandinSeat seat = StandinSeat::Damage)
{
    FamilyStandin row;
    row.family = head;
    row.outName = out;
    row.inName = in;
    row.seat = seat;
    row.reason = "crafts";
    return row;
}

std::vector<std::string> const kRoster{"Grug", "Og", "Ugga", "Grog", "Bork", "Zug", "Oz"};
std::map<std::string, std::string> const kFamilies{
    {"Grug", "Grug"}, {"Og", "Grug"}, {"Ugga", "Grug"}, {"Grog", "Grug"},
    {"Bork", "Grug"}, {"Zug", "Zug"}, {"Oz", "Zug"}};

DcArmingSubject const* Find(std::vector<DcArmingSubject> const& subjects,
                            std::string const& name)
{
    for (DcArmingSubject const& s : subjects)
        if (s.name == name)
            return &s;
    return nullptr;
}

size_t Count(std::vector<DcArmingSubject> const& subjects, std::string const& name)
{
    size_t n = 0;
    for (DcArmingSubject const& s : subjects)
        n += s.name == name;
    return n;
}

void TestDecision()
{
    // No stand-in: the roster, each with its family, none a guest.
    {
        std::vector<DcArmingSubject> const s = DcArmingSubjects(kRoster, kFamilies, {});
        Check("no stand-in arms the roster alone", s.size() == kRoster.size());
        Check("roster order is kept", !s.empty() && s.front().name == "Grug");
        DcArmingSubject const* ugga = Find(s, "Ugga");
        Check("a roster member carries its family", ugga && ugga->family == "Grug");
        Check("a roster member is not a guest", ugga && !ugga->guest);
    }

    // The live case: Og sits out, Pogg stands in as dps. Pogg is armed, under
    // the head's family, as a guest.
    {
        std::vector<DcArmingSubject> const s =
            DcArmingSubjects(kRoster, kFamilies, {Row("Grug", "Og", "Pogg")});
        DcArmingSubject const* pogg = Find(s, "Pogg");
        Check("the stand-in guest is armed", pogg != nullptr);
        Check("the guest belongs to the head's family", pogg && pogg->family == "Grug");
        Check("the guest is marked a guest", pogg && pogg->guest);
        Check("guests come after the roster", !s.empty() && s.back().name == "Pogg");
    }

    // A guest in the tank seat is the run's leader: it must be armed.
    {
        std::vector<DcArmingSubject> const s = DcArmingSubjects(
            kRoster, kFamilies, {Row("Zug", "Oz", "Totta", StandinSeat::Tank)});
        DcArmingSubject const* totta = Find(s, "Totta");
        Check("a tank guest is armed", totta && totta->guest && totta->family == "Zug");
    }

    // A sit-out with no guest adds nobody.
    {
        std::vector<DcArmingSubject> const s =
            DcArmingSubjects(kRoster, kFamilies, {Row("Grug", "Og", "")});
        Check("a sit-out with no guest adds nobody", s.size() == kRoster.size());
    }

    // A guest who is on the roster is armed once, as itself.
    {
        std::vector<DcArmingSubject> const s =
            DcArmingSubjects(kRoster, kFamilies, {Row("Grug", "Og", "Oz")});
        Check("a roster guest is armed once", Count(s, "Oz") == 1);
        DcArmingSubject const* oz = Find(s, "Oz");
        Check("a roster guest keeps its own family", oz && oz->family == "Zug" && !oz->guest);
    }

    // A row whose head is not on the roster adds nobody.
    {
        std::vector<DcArmingSubject> const s =
            DcArmingSubjects(kRoster, kFamilies, {Row("Nobody", "Og", "Pogg")});
        Check("a guest of an unknown head is not armed", Find(s, "Pogg") == nullptr);
    }

    // Two families, two guests: each under its own family.
    {
        std::vector<DcArmingSubject> const s = DcArmingSubjects(
            kRoster, kFamilies, {Row("Grug", "Og", "Pogg"), Row("Zug", "Oz", "Totta")});
        DcArmingSubject const* pogg = Find(s, "Pogg");
        DcArmingSubject const* totta = Find(s, "Totta");
        Check("each guest is armed under its own family",
              pogg && totta && pogg->family == "Grug" && totta->family == "Zug");
    }
}

std::string ReadSource()
{
    std::ifstream file("src/mod_overseer.cpp");
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

std::string Between(std::string const& source, std::string const& start,
                    std::string const& end)
{
    std::string::size_type first = source.find(start);
    if (first == std::string::npos)
        return {};
    first += start.size();
    std::string::size_type last = source.find(end, first);
    if (last == std::string::npos)
        return {};
    return source.substr(first, last - first);
}

void TestWiring()
{
    std::string const source = ReadSource();
    Check("source read", !source.empty());
    std::string const drive =
        Between(source, "void DriveDungeonClear()", "void DriveBelowTerrainRecovery()");
    Check("the arming drive is found", !drive.empty());
    Check("the arming drive loops over DcArmingSubjects",
          drive.find("DcArmingSubjects(") != std::string::npos);
    Check("the frozen stand-ins are handed to it",
          drive.find("_frozenStandins") != std::string::npos);
    Check("a guest joins its family's run rather than opening one",
          drive.find("ActiveRunIdOnMap(bot->GetMapId(), subject.family)") != std::string::npos);
    std::string::size_type const guestBranch = drive.find("if (subject.guest)");
    std::string::size_type const open = drive.find("OpenOrTouchRun(");
    Check("the guest branch comes before the run is opened",
          guestBranch != std::string::npos && open != std::string::npos && guestBranch < open);
}
}  // namespace

int main()
{
    TestDecision();
    TestWiring();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
