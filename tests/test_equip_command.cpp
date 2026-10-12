#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Check(char const* label, bool got, bool wanted)
{
    if (got == wanted)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", label, got ? "true" : "false",
                wanted ? "true" : "false");
    ++failures;
}

void CheckText(char const* label, std::string const& got, char const* wanted)
{
    if (got == wanted)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", label, got.c_str(), wanted);
    ++failures;
}
}

int main()
{
    EquipCommand parsed;
    Check("bridge equip form parses", ParseEquipCommand("e Hitem:15211:0", parsed), true);
    Check("entry is extracted", parsed.entry == 15211, true);
    Check("other verbs are ignored", ParseEquipCommand("nc +grind", parsed), false);
    Check("missing entry is rejected", ParseEquipCommand("e Hitem:", parsed), false);
    Check("trailing command text is rejected", ParseEquipCommand("e Hitem:15211 junk", parsed), false);

    EquipRefusal refusal = EquipRefused("EQUIP_ERR_CANT_EQUIP 29");
    CheckText("core refusal is terminal error", refusal.status, "error");
    CheckText("core refusal detail survives", refusal.detail, "EQUIP_ERR_CANT_EQUIP 29");
    EquipRefusal applied = EquipReadBack(true);
    CheckText("worn item is applied", applied.status, "applied");
    EquipRefusal unchanged = EquipReadBack(false);
    CheckText("unworn item is unchanged", unchanged.status, "unchanged");
    CheckText("unworn item explains outcome", unchanged.detail, "the bot did not put the item on");

    return failures ? 1 : 0;
}
