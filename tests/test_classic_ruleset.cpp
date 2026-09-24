/*
 * The classic ruleset's pure half, decided without a world.
 *
 * WHAT IT IS FOR. The operator restricts these worlds to a classic level-60
 * feel: level 60, professions to skill 300, and never Outland (map 530) or
 * Northrend (map 571). Measured on the dev world: a guild dues walk aimed a
 * guild member at an Outland mailbox, and 676 characters carried a 375 skill
 * cap. The adapter asks these questions before it walks anybody or sells a
 * rank; pinned here is:
 *
 *   - The numbers themselves, which the overseer site mirrors in classic.py
 *     and checks against this header.
 *   - Which maps are outside the classic world.
 *   - Which profession ranks and recipes a trainer may still sell.
 *   - That the walk refusal passes through to the trainer and vendor walks
 *     unchanged, and is worth asking again.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace OverseerDecisions;
namespace C = OverseerDecisions::Classic;
namespace M = OverseerDecisions::MailWalkRefusal;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckNumber(char const* what, uint64_t got, uint64_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %llu, wanted %llu\n", what,
                static_cast<unsigned long long>(got), static_cast<unsigned long long>(want));
    ++failures;
}

void CheckText(char const* what, std::string const& got, char const* want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: '%s', wanted '%s'\n", what, got.c_str(), want);
    ++failures;
}

void TheNumbers()
{
    CheckNumber("the level cap", C::LEVEL_CAP, 60);
    CheckNumber("the profession cap", C::PROFESSION_SKILL_CAP, 300);
    CheckNumber("Outland", C::OUTLAND_MAP_ID, 530);
    CheckNumber("Northrend", C::NORTHREND_MAP_ID, 571);
}

void OnlyOutlandAndNorthrendAreOutside()
{
    Check("Eastern Kingdoms is classic", C::IsExpansionContinent(0), false);
    Check("Kalimdor is classic", C::IsExpansionContinent(1), false);
    Check("Molten Core is classic", C::IsExpansionContinent(409), false);
    Check("Blackrock Depths is classic", C::IsExpansionContinent(230), false);
    Check("Outland is outside", C::IsExpansionContinent(530), true);
    Check("Northrend is outside", C::IsExpansionContinent(571), true);
}

void ArtisanIsTheLastRank()
{
    // SpellLearnSkillNode::maxvalue is step x 75.
    Check("Apprentice", C::ClassicRankAllowed(75), true);
    Check("Journeyman", C::ClassicRankAllowed(150), true);
    Check("Expert", C::ClassicRankAllowed(225), true);
    Check("Artisan", C::ClassicRankAllowed(300), true);
    Check("Master", C::ClassicRankAllowed(375), false);
    Check("Grand Master", C::ClassicRankAllowed(450), false);
    Check("a spell that is not a rank is not judged", C::ClassicRankAllowed(0), true);
}

void RecipesStopAtThreeHundred()
{
    Check("a first recipe", C::ClassicRecipeAllowed(0), true);
    Check("Mooncloth at 250", C::ClassicRecipeAllowed(250), true);
    Check("a recipe at 300", C::ClassicRecipeAllowed(300), true);
    Check("Bolt of Netherweave's 300 is inside, its bag is not",
          C::ClassicRecipeAllowed(315), false);
    Check("Frostweave at 350", C::ClassicRecipeAllowed(350), false);
}

void TheWalkRefusal()
{
    CheckText("the mailbox walk names it", M::ExpansionContinent,
              "character stands in Outland or Northrend, outside the classic world");
    CheckText("the trainer walk passes it through",
              WalkRefusalFor(WalkGoal::Trainer, M::ExpansionContinent), M::ExpansionContinent);
    CheckText("the vendor walk passes it through",
              WalkRefusalFor(WalkGoal::Vendor, M::ExpansionContinent), M::ExpansionContinent);
    Check("a random bot there is moved home by its next teleport, so ask again",
          MailWalkRefusalRetryable(M::ExpansionContinent), true);
    Check("and so is an errand walk", ErrandWalkRefusalRetryable(M::ExpansionContinent), true);
}

}  // namespace

int main()
{
    TheNumbers();
    OnlyOutlandAndNorthrendAreOutside();
    ArtisanIsTheLastRank();
    RecipesStopAtThreeHundred();
    TheWalkRefusal();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the classic ruleset holds\n");
    return EXIT_SUCCESS;
}
