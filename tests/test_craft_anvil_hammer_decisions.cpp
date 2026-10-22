/*
 * The Blacksmithing Anvil + Hammer + Green Dye decisions (#524) are written
 * down in docs/design/profession-crafting-drive.md, and DriveCraft's own
 * comment points at them. Pin both, so a later change to how the drive treats
 * a spell-focus or equipped-tool refusal has to update the decision, not leave
 * it contradicting the code.
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
    Check("the Anvil decision names the focus field", Has(doc, "RequiresSpellFocus"));
    Check("the Anvil decision says the recipes are out of scope for now",
          Has(doc, "Anvil-gated recipes are OUT OF SCOPE"));
    Check("the Hammer decision says EquippedItemClass is not broad",
          Has(doc, "EquippedItemClass") && Has(doc, "is not broad enough"));
    Check("the Hammer design covers a crash mid-swap", Has(doc, "crash mid-swap"));
    Check("the Hammer design covers combat and bag space",
          Has(doc, "combat") && Has(doc, "bag space"));
    Check("the reagent decision names Green Dye", Has(doc, "Green Dye"));
    Check("the reagent decision keeps craft reagents out of kind='buy'",
          Has(doc, "craft reagents stay out of scope for town-trip buying"));
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
