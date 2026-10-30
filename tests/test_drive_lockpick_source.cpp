/*
 * Source contract for DriveLockpick (follow-up to the review of #1114).
 *
 * The decision is proved in test_drive_lockpick.cpp. What only the adapter
 * can get wrong is feeding it. The review found two paths the decision test
 * cannot reach:
 *
 *  - "no target in zone": FindNearestLockpickTarget returns nullptr. The old
 *    code did `if (!target) continue;` inline; the decision only sees a
 *    boolean, so a regression back to a raw null-dereference or an early
 *    exit would pass the decision test. Pinned here: the drive derives
 *    hasTarget from the pointer, asks DecideLockpickErrand, and dereferences
 *    the target only behind that decision.
 *  - "no skill-up": the cast ran but Lockpicking did not rise. Pinned here:
 *    the cast branch compares the skill read-back and never clears the
 *    errand on a failed attempt (only the level-cap check clears it).
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int Fail(char const* what)
{
    std::cerr << "FAIL " << what << "\n";
    return EXIT_FAILURE;
}

// The text of the member function that starts at `signature`, up to the next
// line that closes a member function at the class's indentation.
std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

std::size_t Count(std::string const& text, char const* needle)
{
    std::size_t n = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos;
         at = text.find(needle, at + 1))
        ++n;
    return n;
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const drive = Body(text, "void DriveLockpick()");
    if (drive.empty())
        return Fail("cannot find DriveLockpick");

    // No target: the null pointer becomes hasTarget=false and is decided.
    std::size_t const find = drive.find("FindNearestLockpickTarget(bot)");
    std::size_t const has = drive.find("hasTarget = (target != nullptr)", find);
    std::size_t const decide = drive.find("DecideLockpickErrand(hasTarget", has);
    if (find == std::string::npos || has == std::string::npos || decide == std::string::npos)
        return Fail("DriveLockpick must turn a null target into hasTarget and ask DecideLockpickErrand");

    // Nothing between the find and the decision may skip or dereference the target.
    std::string const between = drive.substr(find, decide - find);
    if (between.find("continue") != std::string::npos
        || between.find("target->GetGUID") != std::string::npos
        || between.find("ClearLockpickErrand") != std::string::npos)
        return Fail("a null target must reach the decision, not exit or clear the errand first");

    // Only a Cast step reaches the cast: the cast sits after the switch.
    std::size_t const castCase = drive.find("LockpickErrandStep::Cast:", decide);
    std::size_t const cast = drive.find("DriveChestOpen(", decide);
    if (castCase == std::string::npos || cast == std::string::npos || cast < castCase)
        return Fail("the Pick Lock cast must come after the Cast step of the decision");

    // No skill-up: the cast is judged by the read-back and never clears the errand.
    std::string const afterCast = drive.substr(cast);
    if (afterCast.find("after > before") == std::string::npos)
        return Fail("the cast must be judged by the Lockpicking skill read-back");
    if (afterCast.find("did not rise") == std::string::npos)
        return Fail("a cast with no skill-up must be logged as leaving the errand standing");
    if (afterCast.find("ClearLockpickErrand") != std::string::npos)
        return Fail("a failed or unrewarded cast must not clear the errand");

    // Only the level-cap check clears the errand.
    if (Count(drive, "ClearLockpickErrand(name)") != 1)
        return Fail("DriveLockpick must clear the errand in exactly one place, the level cap");

    // The zone-sweep predicate routes through IsLockpickTargetCandidate and
    // passes isSpawned and the zone comparison; dropping either input fails.
    std::size_t const checkStart = text.find("struct LockboxNearbyCheck");
    std::size_t const checkEnd = text.find("};", checkStart);
    if (checkStart == std::string::npos || checkEnd == std::string::npos)
        return Fail("cannot find the LockboxNearbyCheck predicate");
    std::string const checkBody = text.substr(checkStart, checkEnd - checkStart);
    if (checkBody.find("IsLockpickTargetCandidate(") == std::string::npos)
        return Fail("LockboxNearbyCheck must route its decision through IsLockpickTargetCandidate");
    if (checkBody.find("go->isSpawned()") == std::string::npos)
        return Fail("LockboxNearbyCheck must pass isSpawned to IsLockpickTargetCandidate");
    if (checkBody.find("go->GetZoneId() == from->GetZoneId()") == std::string::npos)
        return Fail("LockboxNearbyCheck must pass the zone comparison to IsLockpickTargetCandidate");

    std::cout << "test_drive_lockpick_source: all cases passed\n";
    return EXIT_SUCCESS;
}
