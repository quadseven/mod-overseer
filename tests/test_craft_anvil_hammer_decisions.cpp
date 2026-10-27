/*
 * The Blacksmithing Anvil + Hammer + Green Dye decisions (#524) are written
 * down in docs/design/profession-crafting-drive.md, and DriveCraft's own
 * comment points at them. Pin both, so a later change to how the drive treats
 * a spell-focus or tool refusal has to update the decision, not leave it
 * contradicting the code. The behaviour itself is test_craft_anvil_hammer.cpp.
 *
 * Reads docs and source as text (run from the repo root); needs no link.
 */

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

bool Slurp(char const* path, std::string& out)
{
    std::ifstream in(path);
    if (!in)
    {
        std::printf("FAIL could not read %s (run from the repo root)\n", path);
        return false;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    out = buf.str();
    return true;
}

bool Has(std::string const& hay, char const* needle)
{
    return hay.find(needle) != std::string::npos;
}

} // namespace

int main()
{
    std::string doc, src;
    if (!Slurp("docs/design/profession-crafting-drive.md", doc) ||
        !Slurp("src/mod_overseer.cpp", src))
        return 1;

    Check("the doc has the Anvil/Hammer/dye section",
          Has(doc, "## Anvil, Hammer and reagent-gated Blacksmithing recipes (#524)"));
    Check("the Anvil is focus 1, read from data0, not data1",
          Has(doc, "spell focus **1**") && Has(doc, "`spellFocus.focusId`) = 1") &&
              Has(doc, "`spellFocus.dist`"));
    Check("the Hammer is a tool category, not an equipped weapon",
          Has(doc, "`RequiredTotemCategoryID` 162") && Has(doc, "HasItemTotemCategory") &&
              Has(doc, "nothing the character wields is ever swapped"));
    Check("the operator's decision is recorded",
          Has(doc, "keep it in the crafter's own bank, and withdraw it when crafting"));
    Check("the Hammer goes back to the bank", Has(doc, "`ReturnCraftTools` puts it back"));
    Check("the Anvil walk is bounded to a town", Has(doc, "`CRAFT_FOCUS_WALK_LIMIT_YARDS` (400"));
    Check("the reagent decision names Green Dye", Has(doc, "Green Dye"));
    Check("Green Dye is bought by the planner's buy rows", Has(doc, "Green Dye joins that table"));
    Check("the old swap design is gone",
          !Has(doc, "crash mid-swap") && !Has(doc, "Anvil-gated recipes are OUT OF SCOPE"));
    Check("DriveCraft's comment points at the decisions",
          Has(src, "#524") && Has(src, "Anvil, Hammer and reagent-gated"));

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok test_craft_anvil_hammer_decisions\n");
    return 0;
}
