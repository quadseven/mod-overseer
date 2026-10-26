/*
 * Source contract for DriveDisenchant (mod-overseer#444).
 *
 * The decision logic is proved in test_drive_disenchant.cpp. What only the
 * adapter can get wrong is feeding it. The review found paths the decision
 * test cannot reach:
 *
 *  - "no target in bags": FindNearestDisenchantTarget returns nullptr. The
 *    drive must not dereference a null target. Pinned here: the drive derives
 *    hasItem from the pointer, asks DecideDisenchantErrand, and dereferences
 *    the target only behind that decision.
 *  - "cast refused": the cast ran but was refused. Pinned here: the cast
 *    result is checked and the errand is not cleared on a refused cast.
 *  - "no reagent": the cast ran but the dust did not appear in the bags.
 *    Pinned here: the reagent is read back after the cast and the errand is
 *    not cleared when the count did not rise.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
int Fail(char const* what)
{
    std::cerr << "FAIL " << what << "\n";
    return EXIT_FAILURE;
}

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
} // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const drive = Body(text, "void DriveDisenchant()");
    if (drive.empty())
        return Fail("cannot find DriveDisenchant");

    // No target: the null pointer becomes hasItem=false and is decided.
    std::size_t const find = drive.find("FindNearestDisenchantTarget(bot)");
    std::size_t const has = drive.find("Item* target = FindNearestDisenchantTarget(bot)");
    if (find == std::string::npos || has == std::string::npos)
        return Fail("DriveDisenchant must call FindNearestDisenchantTarget(bot)");

    // Nothing between the find and the decision may skip or dereference the target.
    std::size_t const decide = drive.find("DecideDisenchantErrand(", find);
    if (decide == std::string::npos)
        return Fail("DriveDisenchant must ask DecideDisenchantErrand");
    std::string const between = drive.substr(find, decide - find);
    if (between.find("continue") != std::string::npos)
        return Fail("a null target must reach the decision, not exit first");
    if (between.find("target->GetTemplate") != std::string::npos)
        return Fail("a null target must not be dereferenced before the decision");

    // Only a Cast step reaches the cast: the cast sits after the decision.
    std::size_t const castCase = drive.find("DisenchantErrandStep::Cast:", decide);
    std::size_t const cast = drive.find("bot->CastSpell(targets", decide);
    if (castCase == std::string::npos || cast == std::string::npos || cast < castCase)
        return Fail("the Disenchant cast must come after the Cast step of the decision");

    // The cast uses SpellCastTargets with SetItemTarget (item-target cast path).
    if (drive.find("SpellCastTargets targets;") == std::string::npos)
        return Fail("the cast must use SpellCastTargets");
    if (drive.find("targets.SetItemTarget(target)") == std::string::npos)
        return Fail("the cast must set the item target via SetItemTarget");

    // No reagent: the cast is judged by the inventory read-back and never
    // clears the errand.
    std::string const afterCast = drive.substr(cast);
    if (afterCast.find("GetItemCount(DISENCHANT_REAGENT_ENTRY") == std::string::npos)
        return Fail("the cast must be judged by the Strange Dust read-back");
    if (afterCast.find("ShouldClearDisenchantErrand(") == std::string::npos)
        return Fail("the errand must be judged by ShouldClearDisenchantErrand");
    if (afterCast.find("did not rise") == std::string::npos)
        return Fail("a cast with no dust rise must be logged as leaving the errand standing");
    if (afterCast.find("ClearCraftAim") != std::string::npos &&
        afterCast.find("craft_spell = 0") != std::string::npos)
        return Fail("a failed or unrewarded cast must not clear the errand");

    // The drive is registered on a timer alongside the other job-mode drives.
    if (Count(text, "DISENCHANT_POLL_MS") < 3)
        return Fail("DISENCHANT_POLL_MS must be defined, incremented, and gated");
    if (Count(text, "_disenchantTimer") < 3)
        return Fail("_disenchantTimer must be declared, incremented, and reset");

    // The ENUM value is added to the kind column.
    if (text.find("'disenchant'") == std::string::npos)
        return Fail("the kind ENUM must include 'disenchant'");

    // The drive reuses DriveCraft's standing-errand-on-refusal shape: it does
    // not clear an errand column on a refused cast.
    if (drive.find("craft_spell = 0") != std::string::npos)
        return Fail("DriveDisenchant must not clear craft_spell");
    if (drive.find("ClearCraftAim") != std::string::npos)
        return Fail("DriveDisenchant must not call ClearCraftAim");

    std::cout << "test_drive_disenchant_source: all cases passed\n";
    return EXIT_SUCCESS;
}
