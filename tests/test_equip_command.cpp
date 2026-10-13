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
    Check("zero entry is rejected", ParseEquipCommand("e Hitem:0", parsed), false);
    Check("zero entry with link suffix is rejected", ParseEquipCommand("e Hitem:0:0", parsed), false);
    Check("padded zero entry is rejected", ParseEquipCommand("e Hitem:000:0:0", parsed), false);
    Check("padded entry parses", ParseEquipCommand("e Hitem:0015211:0", parsed) && parsed.entry == 15211, true);
    Check("overflowing entry is rejected", ParseEquipCommand("e Hitem:999999999999999999999999", parsed), false);
    Check("largest uint32 entry parses", ParseEquipCommand("e Hitem:4294967295:0", parsed) && parsed.entry == 4294967295u, true);
    Check("entry past uint32 is rejected", ParseEquipCommand("e Hitem:4294967296:0", parsed), false);
    Check("trailing command text is rejected", ParseEquipCommand("e Hitem:15211 junk", parsed), false);
    Check("bare link without entry is rejected", ParseEquipCommand("e Hitem", parsed), false);
    Check("trailing bare colon is rejected", ParseEquipCommand("e Hitem:15211:", parsed), false);
    Check("empty middle suffix field is rejected", ParseEquipCommand("e Hitem:15211::0", parsed), false);
    Check("multi-field link suffix parses", ParseEquipCommand("e Hitem:15211:0:0:0", parsed) && parsed.entry == 15211, true);

    EquipRefusal refusal = EquipRefused("EQUIP_ERR_CANT_EQUIP 29");
    CheckText("core refusal is terminal error", refusal.status, "error");
    CheckText("core refusal detail survives", refusal.detail, "EQUIP_ERR_CANT_EQUIP 29");
    // The command loop in mod_overseer.cpp holds the row's status as a
    // `char const*` and assigns the refusal's status to it; this line is that
    // assignment and does not compile if the status is not a literal pointer.
    char const* rowStatus = "verifying";
    rowStatus = refusal.status;
    CheckText("refusal status assigns to the row status", rowStatus, "error");
    EquipRefusal applied = EquipReadBack(true);
    CheckText("worn item is applied", applied.status, "applied");
    EquipRefusal unchanged = EquipReadBack(false);
    CheckText("unworn item is unchanged", unchanged.status, "unchanged");
    CheckText("unworn item explains outcome", unchanged.detail, "the bot did not put the item on");

    return failures ? 1 : 0;
}
