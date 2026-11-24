/*
 * A town trip for bag room that made no bag room is not opened again for the
 * same member (wow-overseer#1261).
 *
 * MEASURED ON THE DEV REALM 2026-11-24. Both families stood stalled for over two
 * hours while the town trip reopened every cooldown on "3 of 5 member(s) want
 * one and 3 of those cannot carry on without it (bags)". Each visit ended
 * "stood at the 'vendor' ... and NOTHING CHANGED - 0 free bag slot(s)": the
 * stacks those members carry are kept, banked or auctioned, never sold to a
 * vendor, and the bridge had counted 148 to 221 such visits per member. The
 * Zug family's leader was taken off its walk to the raid door by each new trip.
 *
 * The rule: a member a vendor visit left no roomier is not a bags driver until
 * its free slots rise past that reading, or two hours pass and the visit is
 * tried once more. Broken gear still opens a trip for it.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using OverseerDecisions::CounterRole;
using OverseerDecisions::FruitlessBagsMark;
using OverseerDecisions::NoteTownTripBags;
using OverseerDecisions::PlanTownTrip;
using OverseerDecisions::TownNeed;
using OverseerDecisions::TownReason;
using OverseerDecisions::TownTripBagsFruitless;
using OverseerDecisions::TownTripLimits;
using OverseerDecisions::TownTripPlan;

namespace
{

int failures = 0;

void Expect(bool ok, char const* what)
{
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

TownNeed Need(unsigned freeSlots, unsigned damaged = 0, unsigned broken = 0)
{
    TownNeed n;
    n.present = true;
    n.freeBagSlots = freeSlots;
    n.damagedItems = damaged;
    n.brokenItems = broken;
    return n;
}

TownTripLimits const LIMITS{};
time_t const T0 = 1000000;

void NoMarkIsNotFruitless()
{
    Expect(!TownTripBagsFruitless(Need(0), FruitlessBagsMark{}, LIMITS, T0),
           "a member with no mark is not fruitless");
}

void MarkHoldsAtTheSameRoom()
{
    FruitlessBagsMark mark{true, 0, T0};
    Expect(TownTripBagsFruitless(Need(0), mark, LIMITS, T0 + 60),
           "same free slots a minute later stays fruitless");
    Expect(TownTripBagsFruitless(Need(0), mark, LIMITS, T0 + LIMITS.fruitlessBagsSeconds - 1),
           "still fruitless one second before the bound");
}

void MarkEndsWhenRoomRises()
{
    FruitlessBagsMark mark{true, 1, T0};
    Expect(!TownTripBagsFruitless(Need(2), mark, LIMITS, T0 + 60),
           "a member with more room than the fruitless visit left is asked again");
    Expect(TownTripBagsFruitless(Need(0), mark, LIMITS, T0 + 60),
           "a member with less room is still fruitless: the vendor could not help");
}

void MarkEndsAfterTheBound()
{
    FruitlessBagsMark mark{true, 0, T0};
    Expect(!TownTripBagsFruitless(Need(0), mark, LIMITS, T0 + LIMITS.fruitlessBagsSeconds),
           "after the bound the visit is tried once more");
}

void DefaultBoundIsTwoHours()
{
    Expect(LIMITS.fruitlessBagsSeconds == 2 * 3600, "the bound is two hours");
}

void VendorVisitThatMadeNoRoomMarks()
{
    FruitlessBagsMark const m = NoteTownTripBags(
        Need(0), Need(0), 1907620, 1907620, CounterRole::Vendor, true, LIMITS, T0,
        FruitlessBagsMark{});
    Expect(m.set && m.freeBagSlots == 0 && m.at == T0,
           "a vendor visit at the floor that freed nothing marks the member");
}

void RepairSpendingIsStillNoRoom()
{
    FruitlessBagsMark const m = NoteTownTripBags(
        Need(1, 2), Need(1), 5000, 4200, CounterRole::Vendor, true, LIMITS, T0,
        FruitlessBagsMark{});
    Expect(m.set && m.freeBagSlots == 1,
           "purse down and slots flat is no bag room made");
}

void VisitThatMadeRoomClears()
{
    FruitlessBagsMark const prev{true, 0, T0 - 100};
    FruitlessBagsMark const roomier = NoteTownTripBags(
        Need(0), Need(4), 100, 100, CounterRole::Vendor, true, LIMITS, T0, prev);
    Expect(!roomier.set, "slots rose: the mark is cleared");
    FruitlessBagsMark const sold = NoteTownTripBags(
        Need(0), Need(0), 100, 250, CounterRole::Vendor, true, LIMITS, T0, prev);
    Expect(!sold.set, "purse rose: something sold, the mark is cleared");
}

void NotAtTheCounterProvesNothing()
{
    FruitlessBagsMark const prev{true, 3, T0 - 100};
    FruitlessBagsMark const m = NoteTownTripBags(
        Need(0), Need(0), 100, 100, CounterRole::Vendor, false, LIMITS, T0, prev);
    Expect(m.set && m.freeBagSlots == 3 && m.at == T0 - 100,
           "a member that never stood at the counter keeps its previous mark");
    FruitlessBagsMark const none = NoteTownTripBags(
        Need(0), Need(0), 100, 100, CounterRole::Vendor, false, LIMITS, T0,
        FruitlessBagsMark{});
    Expect(!none.set, "and gains none");
}

void RepairerTripIsNotABagVisit()
{
    FruitlessBagsMark const m = NoteTownTripBags(
        Need(0, 1, 1), Need(0), 100, 50, CounterRole::Repairer, true, LIMITS, T0,
        FruitlessBagsMark{});
    Expect(!m.set, "a repairer is not asked to make bag room");
}

void MemberAboveTheFloorIsNotMarked()
{
    FruitlessBagsMark const m = NoteTownTripBags(
        Need(10), Need(10), 100, 100, CounterRole::Vendor, true, LIMITS, T0,
        FruitlessBagsMark{});
    Expect(!m.set, "a member that did not want bag room is not marked");
}

void FruitlessMembersOpenNoTrip()
{
    std::vector<TownNeed> const family{Need(0), Need(0), Need(0), Need(8), Need(7)};
    std::vector<bool> const fruitless{true, true, true, false, false};
    TownTripPlan const plan = PlanTownTrip(family, LIMITS, LIMITS.cooldownSeconds, false,
                                           fruitless);
    Expect(!plan.go, "three members a vendor could not help open no vendor trip");
    Expect(plan.membersDriving == 0, "and none of them drives one");
}

void OneUnhelpedMemberStillOpensATrip()
{
    std::vector<TownNeed> const family{Need(0), Need(2), Need(9)};
    std::vector<bool> const fruitless{true, false, false};
    TownTripPlan const plan = PlanTownTrip(family, LIMITS, LIMITS.cooldownSeconds, false,
                                           fruitless);
    Expect(plan.go && plan.role == CounterRole::Vendor && plan.membersDriving == 1,
           "a member not yet tried still opens the trip, alone");
}

void BrokenGearStillOpensATrip()
{
    std::vector<TownNeed> const family{Need(0, 1, 1), Need(9)};
    std::vector<bool> const fruitless{true, false};
    TownTripPlan const plan = PlanTownTrip(family, LIMITS, LIMITS.cooldownSeconds, false,
                                           fruitless);
    Expect(plan.go && plan.role == CounterRole::Repairer && plan.reason == TownReason::Broken,
           "broken gear is a repair trip whatever the bags say");
}

void WornFruitlessMemberIsOwedNotDriving()
{
    std::vector<TownNeed> const family{Need(0, 2), Need(9)};
    std::vector<bool> const fruitless{true, false};
    TownTripPlan const plan = PlanTownTrip(family, LIMITS, LIMITS.cooldownSeconds, false,
                                           fruitless);
    Expect(!plan.go && plan.membersOwed == 1 && plan.membersDriving == 0,
           "a worn fruitless member is owed a repair but opens nothing");
}

void ShortFlagListChangesNothing()
{
    std::vector<TownNeed> const family{Need(0), Need(9)};
    TownTripPlan const plain = PlanTownTrip(family, LIMITS, LIMITS.cooldownSeconds, false);
    TownTripPlan const flagged = PlanTownTrip(family, LIMITS, LIMITS.cooldownSeconds, false,
                                              std::vector<bool>{});
    Expect(plain.go == flagged.go && plain.role == flagged.role &&
               plain.membersDriving == flagged.membersDriving,
           "an empty flag list is the old plan");
}

} // namespace

int main()
{
    NoMarkIsNotFruitless();
    MarkHoldsAtTheSameRoom();
    MarkEndsWhenRoomRises();
    MarkEndsAfterTheBound();
    DefaultBoundIsTwoHours();
    VendorVisitThatMadeNoRoomMarks();
    RepairSpendingIsStillNoRoom();
    VisitThatMadeRoomClears();
    NotAtTheCounterProvesNothing();
    RepairerTripIsNotABagVisit();
    MemberAboveTheFloorIsNotMarked();
    FruitlessMembersOpenNoTrip();
    OneUnhelpedMemberStillOpensATrip();
    BrokenGearStillOpensATrip();
    WornFruitlessMemberIsOwedNotDriving();
    ShortFlagListChangesNothing();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
