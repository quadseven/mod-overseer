/*
 * A run is not opened at a door the party cannot get through (mod-overseer#578).
 *
 * Two prerequisites the world enforces and the coordinator never checked: the
 * core's door minimum (dungeon_access_template), which refuses an under-level
 * member at the knock, and a locked door on the approach, which does not open
 * without its key. The numbers below are the world's own: the door minimums
 * read from dungeon_access_template, and lock 1562 read from Lock.dbc, whose
 * LOCK_KEY_ITEM case is item 18249, the Crescent Key.
 *
 * The last part reads src/mod_overseer.cpp (run from the repo root) to pin the
 * rows that name a locked door and the one call that asks.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using OverseerDecisions::DoorCandidate;
using OverseerDecisions::DoorPrerequisites;
using OverseerDecisions::DoorReadiness;
using OverseerDecisions::DoorReadinessReading;
using OverseerDecisions::DoorReadinessReason;
using OverseerDecisions::ReadDoorReadiness;

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

void CheckString(char const* what, std::string const& got, std::string const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s:\n  got  '%s'\n  want '%s'\n", what, got.c_str(), want.c_str());
    ++failures;
}

DoorCandidate Member(char const* name, std::uint32_t level, bool carriesKey = false)
{
    DoorCandidate member;
    member.name = name;
    member.level = level;
    member.carriesKey = carriesKey;
    return member;
}

// dungeon_access_template id 6: Blackfathom Deeps, map 48, min_level 19.
DoorPrerequisites Blackfathom()
{
    DoorPrerequisites door;
    door.minLevel = 19;
    return door;
}

// Dire Maul, West Wing [North]: map 429, min_level 45, and gameobject 177189 on
// the approach, locked (flags 34), lock 1562 -> item 18249.
DoorPrerequisites DireMaulWestNorth()
{
    DoorPrerequisites door;
    door.minLevel = 45;
    door.keyDoorEntry = 177189;
    door.keyItems = {18249};
    return door;
}

void AFamilyThatStraddlesTheFloorIsRefused()
{
    // The Horde family as #578 measured it, about 16 to 20.
    std::vector<DoorCandidate> const party = {
        Member("Zug", 20), Member("Zrog", 19), Member("Zork", 17), Member("Oz", 16),
        Member("Uzza", 19)};
    DoorReadinessReading const reading = ReadDoorReadiness(Blackfathom(), party);
    Check("blackfathom under the floor is refused",
          reading.verdict == DoorReadiness::UnderLevel);
    Check("and names exactly the two under it", reading.underLevel.size() == 2);
    CheckString("blackfathom reason", DoorReadinessReason(reading, Blackfathom(), party),
                "the door minimum is level 19 and 'Zork' is 17 and 'Oz' is 16");

    // The same family a few levels on is let through; so is a member exactly on it.
    std::vector<DoorCandidate> const later = {
        Member("Zug", 29), Member("Zrog", 27), Member("Zork", 26), Member("Oz", 27),
        Member("Uzza", 19)};
    Check("on the floor is through",
          ReadDoorReadiness(Blackfathom(), later).verdict == DoorReadiness::Ready);
    CheckString("and says nothing", DoorReadinessReason(ReadDoorReadiness(Blackfathom(), later),
                                                        Blackfathom(), later),
                "");
}

void ALockedDoorNeedsOneKeyInTheParty()
{
    std::vector<DoorCandidate> party = {Member("Grug", 60), Member("Bork", 60),
                                        Member("Og", 60), Member("Grog", 60),
                                        Member("Ugga", 60)};
    DoorReadinessReading const none = ReadDoorReadiness(DireMaulWestNorth(), party);
    Check("nobody carries the Crescent Key: refused", none.verdict == DoorReadiness::NoKey);
    CheckString("no key reason", DoorReadinessReason(none, DireMaulWestNorth(), party),
                "gameobject 177189 on the approach is locked and nobody in the party "
                "carries item 18249");

    party[3].carriesKey = true;
    Check("one member carrying it is enough",
          ReadDoorReadiness(DireMaulWestNorth(), party).verdict == DoorReadiness::Ready);

    // The floor is asked first: an under-level member is the reason even
    // when the key is also missing.
    party[3].carriesKey = false;
    party[4].level = 44;
    Check("under the floor outranks the key",
          ReadDoorReadiness(DireMaulWestNorth(), party).verdict == DoorReadiness::UnderLevel);

    // A door named with a lock that resolved to no key is refused, not waved on.
    DoorPrerequisites unread = DireMaulWestNorth();
    unread.keyItems.clear();
    party[4].level = 60;
    Check("an unreadable lock is refused",
          ReadDoorReadiness(unread, party).verdict == DoorReadiness::KeyUnknown);

    // And a door with no key named asks nothing of anybody.
    DoorPrerequisites east;
    east.minLevel = 45;
    Check("the East wing needs no key",
          ReadDoorReadiness(east, party).verdict == DoorReadiness::Ready);
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::stringstream text;
    text << source.rdbuf();
    return text.str();
}

void TheRowsAndTheCallAreWired()
{
    std::string const source = ReadModule();
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }
    auto has = [&](char const* what, char const* text) {
        Check(what, source.find(text) != std::string::npos);
    };
    has("West Wing [North] names its door",
        "{\"dire-maul-west-north\", 1, 3187, 429, 3191, 0.f, 0.f, 0.f, {}, 0.f, 0.f, 0.f,\n"
        "             nullptr, 177189},");
    has("West Wing [South] names its door",
        "{\"dire-maul-west-south\", 1, 3186, 429, 3190, 0.f, 0.f, 0.f, {}, 0.f, 0.f, 0.f,\n"
        "             nullptr, 177188},");
    has("the North Wing names its door",
        "{\"dire-maul-north\", 1, 3189, 429, 3193, 0.f, 0.f, 0.f, {}, 0.f, 0.f, 0.f,\n"
        "             nullptr, 177192},");
    has("the East wing's outer door names none",
        "{\"dire-maul-east-east\", 1, 3185, 429, 3196, 0.f, 0.f, 0.f},");
    has("the level floor is the core's own row",
        "sObjectMgr->GetAccessRequirement(portal.insideMapId, DUNGEON_DIFFICULTY_NORMAL)");
    has("the key is read from the door's lock",
        "sLockStore.LookupEntry(door->GetLockId())");
    has("IDLE asks before a run opens",
        "OverseerDecisions::ReadDoorReadiness(prerequisites, party);");
}

}  // namespace

int main()
{
    AFamilyThatStraddlesTheFloorIsRefused();
    ALockedDoorNeedsOneKeyInTheParty();
    TheRowsAndTheCallAreWired();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
