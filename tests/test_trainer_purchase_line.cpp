/*
 * The proof line written when a trainer rank or recipe is actually bought
 * (mod-overseer#1124): its shape is pinned here, and the call sites in
 * src/mod_overseer.cpp (run from the repo root) are pinned to write it only
 * after the read-back shows the purchase happened.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

std::string Source()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Text of the function whose definition starts at `head`, up to `tail`.
std::string Between(std::string const& s, char const* head, char const* tail)
{
    size_t a = s.find(head);
    if (a == std::string::npos)
        return std::string();
    size_t b = s.find(tail, a);
    return b == std::string::npos ? std::string() : s.substr(a, b - a);
}

} // namespace

int main()
{
    std::string const rank = OverseerDecisions::TrainerPurchaseLine(
        "Alice", "rank", "Tailoring", 197, 3911, "Journeyman Tailoring", 150, 1000);
    Check("rank line shape",
          rank == "overseer: trainer_purchase member='Alice' kind=rank skill=Tailoring (197) "
                  "spell=3911 'Journeyman Tailoring' max_skill=150 paid_copper=1000");

    std::string const recipe = OverseerDecisions::TrainerPurchaseLine(
        "Bob", "recipe", "First Aid", 129, 3276, "Heavy Linen Bandage", 150, 0);
    Check("recipe line shape, a zero price is said as 0",
          recipe == "overseer: trainer_purchase member='Bob' kind=recipe skill=First Aid (129) "
                    "spell=3276 'Heavy Linen Bandage' max_skill=150 paid_copper=0");

    std::string const src = Source();
    Check("source readable", !src.empty());

    // Success path only: each writer sits after its read-back, not before it.
    std::string const buyRecipes =
        Between(src, "static uint32 BuyRecipes(", "// Every enabled character the roster");
    Check("BuyRecipes writes the line after HasSpell",
          buyRecipes.find("TrainerPurchaseLine") != std::string::npos &&
              buyRecipes.find("HasSpell") < buyRecipes.find("TrainerPurchaseLine"));

    std::string const rankFn =
        Between(src, "bool const succeeded = alreadyHasSkill", "ClearLearnAim(name);\n        return true;\n    }\n\n    // Buy the ONE");
    Check("rank purchase writes the line after the succeeded check",
          rankFn.find("if (!succeeded)") != std::string::npos &&
              rankFn.find("if (!succeeded)") < rankFn.find("TrainerPurchaseLine"));

    std::string const named = Between(src, "bool BuyNamedRecipe(", "bool TrainOnArrival(");
    Check("named recipe writes the line after the HasSpell refusal return",
          named.find("TrainerPurchaseLine") != std::string::npos &&
              named.find("if (!bot->HasSpell(recipe))") < named.find("TrainerPurchaseLine"));

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
