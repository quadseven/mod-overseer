/*
 * The Molten Core door reads each seat's save before ENTER (#1458).
 *
 * A raider saved to another copy of the raid is refused at the door by the
 * core, so the seat is named and left out of the ENTER count instead of
 * holding the raid until the wait ceiling. These functions decide that from
 * instance ids alone; nothing here touches a world or a database.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using OverseerDecisions::RaidBindVerdict;
using OverseerDecisions::RaidBindVerdictFor;
using OverseerDecisions::RaidCreditedEncounters;
using OverseerDecisions::RaidDoor;
using OverseerDecisions::RaidDoorFor;
using OverseerDecisions::RaidSeatBind;
using OverseerDecisions::RaidSeatBindFact;
using OverseerDecisions::RaidSeatBindFor;
using OverseerDecisions::RaidTimelineEvents;
using OverseerDecisions::RaidTimelineSnapshot;
using OverseerDecisions::RunTimelineEvent;

namespace
{

int failures = 0;

void Check(char const* what, bool held)
{
    if (held)
        return;
    ++failures;
    std::printf("FAIL %s\n", what);
}

bool Contains(std::string const& text, char const* part)
{
    return text.find(part) != std::string::npos;
}

void ASeatIsToldByItsInstanceId()
{
    Check("no save is none", RaidSeatBindFor(0, 77) == RaidSeatBind::None);
    Check("no save is none when the group has none", RaidSeatBindFor(0, 0) == RaidSeatBind::None);
    Check("the group's copy is the same copy", RaidSeatBindFor(77, 77) == RaidSeatBind::SameCopy);
    Check("another copy is another copy", RaidSeatBindFor(78, 77) == RaidSeatBind::OtherCopy);
    Check("a save when the group has none yet is another copy",
          RaidSeatBindFor(78, 0) == RaidSeatBind::OtherCopy);
}

void TheVerdictNamesTheMisSavedAndSeatsTheRest()
{
    std::vector<RaidSeatBindFact> const seats{
        {"Head", RaidSeatBind::SameCopy}, {"Fresh", RaidSeatBind::None},
        {"Wrong", RaidSeatBind::OtherCopy}, {"Alsowrong", RaidSeatBind::OtherCopy},
        {"Partner", RaidSeatBind::SameCopy}};
    RaidBindVerdict const verdict = RaidBindVerdictFor(seats);
    Check("three seats may enter", verdict.mayEnter.size() == 3);
    Check("two seats are named", verdict.misSaved.size() == 2);
    Check("the names are the other-copy seats, in seat order",
          verdict.misSaved.size() == 2 && verdict.misSaved[0] == "Wrong" &&
              verdict.misSaved[1] == "Alsowrong");
    Check("an unsaved seat may enter",
          verdict.mayEnter.size() == 3 && verdict.mayEnter[1] == "Fresh");

    std::vector<RaidSeatBindFact> const one{{"A", RaidSeatBind::None}};
    RaidBindVerdict const clean = RaidBindVerdictFor(one);
    Check("a clean raid names nobody", clean.misSaved.empty() && clean.mayEnter.size() == 1);
    RaidBindVerdict const empty = RaidBindVerdictFor({});
    Check("an empty raid is empty", empty.mayEnter.empty() && empty.misSaved.empty());
}

void APartlyClearedCopyIsCreditedFromItsMask()
{
    RaidDoor const* door = RaidDoorFor("moltencore");
    Check("the door exists", door != nullptr);
    if (!door)
        return;
    Check("a fresh copy has none credited", RaidCreditedEncounters(*door, 0, 10) == 0);
    Check("bits 0, 1 and 4 are three", RaidCreditedEncounters(*door, 0x13, 10) == 3);
    Check("a bit past the script's slots is not counted",
          RaidCreditedEncounters(*door, 0x400 | 0x1, 10) == 1);
}

void TheTimelineNamesTheMisSavedAtEnter()
{
    RaidTimelineSnapshot assemble;
    assemble.phase = "ASSEMBLE";
    assemble.keyword = "moltencore";
    RaidTimelineSnapshot enter = assemble;
    enter.phase = "ENTER";
    enter.why = "every raid member in the world is at the door";
    enter.atDoor = 38;
    enter.inRaid = 38;
    enter.misSaved = {"Wrong", "Alsowrong"};
    enter.creditedEncounters = 4;
    enter.encounterSlots = 10;

    std::vector<RunTimelineEvent> const rows = RaidTimelineEvents(assemble, enter);
    bool phase = false;
    bool entered = false;
    bool refused = false;
    for (RunTimelineEvent const& row : rows)
    {
        if (row.kind == "phase")
            phase = true;
        if (row.kind == "enter")
        {
            entered = true;
            Check("the enter row says how many are credited",
                  Contains(row.detail, "4 of 10 encounters already credited"));
            Check("the enter row names the mis-saved",
                  Contains(row.detail, "Wrong") && Contains(row.detail, "Alsowrong"));
            Check("the enter row says the copy is entered as it stands",
                  Contains(row.detail, "enters its copy as is"));
        }
        if (row.kind == "enter_refused")
        {
            refused = true;
            Check("the refusal row names both seats",
                  Contains(row.detail, "Wrong") && Contains(row.detail, "Alsowrong") &&
                      Contains(row.detail, "another copy"));
        }
    }
    Check("the phase row is still written", phase);
    Check("an enter row is written", entered);
    Check("a refusal row is written", refused);

    RaidTimelineSnapshot clean = enter;
    clean.misSaved.clear();
    clean.creditedEncounters = 0;
    bool cleanRefused = false;
    bool cleanEnter = false;
    for (RunTimelineEvent const& row : RaidTimelineEvents(assemble, clean))
    {
        if (row.kind == "enter_refused")
            cleanRefused = true;
        if (row.kind == "enter")
        {
            cleanEnter = true;
            Check("a fresh raid says no encounters are credited",
                  Contains(row.detail, "0 of 10 encounters already credited"));
        }
    }
    Check("a clean raid writes no refusal row", !cleanRefused);
    Check("a clean raid still writes its enter row", cleanEnter);
}

}  // namespace

int main()
{
    ASeatIsToldByItsInstanceId();
    TheVerdictNamesTheMisSavedAndSeatsTheRest();
    APartlyClearedCopyIsCreditedFromItsMask();
    TheTimelineNamesTheMisSavedAtEnter();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::puts("test_raid_door_binds: ok");
    return 0;
}
