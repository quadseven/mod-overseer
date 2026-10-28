/*
 * DriveCraft writes a material-request event when a craft is skipped for a
 * reagent the crafter is short of (#1099): one event naming the character, the
 * recipe, the reagent item and the count SHORT, and no second one while the
 * first is open for that character, recipe and reagent.
 *
 * The decisions are pure (OverseerDecisions::ReagentShortBy,
 * MaterialRequestDetail, OpenMaterialRequests); the adapter half is pinned by
 * reading DriveCraft as text, the way test_craft_anvil_hammer.cpp pins the
 * tool request. Run from the repo root.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::MaterialRequestDetail;
using OverseerDecisions::OpenMaterialRequests;
using OverseerDecisions::ReagentShortBy;

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

bool Has(std::string const& hay, char const* needle)
{
    return hay.find(needle) != std::string::npos;
}

constexpr unsigned RECIPE = 2666;
constexpr unsigned COPPER_BAR = 2840;
constexpr unsigned LINEN_CLOTH = 2589;

void TheCountShort()
{
    Check("holding 2 of 6 is 4 short", ReagentShortBy(6, 2) == 4);
    Check("holding none of 6 is 6 short", ReagentShortBy(6, 0) == 6);
    Check("holding exactly enough is not short", ReagentShortBy(6, 6) == 0);
    Check("holding more than enough is not short, never a wrap-around",
          ReagentShortBy(6, 40) == 0);

    std::string const detail = MaterialRequestDetail(6, 2, "Copper Bar");
    Check("the detail says the count short, not the count needed",
          detail == "short 4x Copper Bar (needs 6, has 2)");
}

void OneRequestWhileOpen()
{
    OpenMaterialRequests open;
    Check("the first skip writes the event", open.Open("Alpha", RECIPE, COPPER_BAR));
    Check("the request is now open", open.IsOpen("Alpha", RECIPE, COPPER_BAR));
    Check("the next skip for the same reagent writes nothing",
          !open.Open("Alpha", RECIPE, COPPER_BAR));
    Check("nor the hundredth", !open.Open("Alpha", RECIPE, COPPER_BAR));
    Check("still one open request", open.Size() == 1);

    Check("another reagent of the same recipe is its own request",
          open.Open("Alpha", RECIPE, LINEN_CLOTH));
    Check("another recipe short of the same reagent is its own request",
          open.Open("Alpha", RECIPE + 1, COPPER_BAR));
    Check("another character short of the same reagent is its own request",
          open.Open("Bravo", RECIPE, COPPER_BAR));
    Check("four open requests", open.Size() == 4);

    open.Close("Alpha", RECIPE, COPPER_BAR);
    Check("holding enough closes that request", !open.IsOpen("Alpha", RECIPE, COPPER_BAR));
    Check("and only that one", open.IsOpen("Alpha", RECIPE, LINEN_CLOTH) &&
                                   open.IsOpen("Bravo", RECIPE, COPPER_BAR));
    Check("short again after it closed asks again", open.Open("Alpha", RECIPE, COPPER_BAR));

    open.Close("Charlie", RECIPE, COPPER_BAR);
    Check("closing a request that was never open changes nothing", open.Size() == 4);
}

void TheAdapterUsesTheGate()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    Check("the module source is readable (run from the repo root)", !source.empty());

    std::size_t const drive = source.find("void DriveCraft()");
    Check("DriveCraft exists", drive != std::string::npos);
    std::size_t const end = source.find("// THE TOOL A RECIPE NEEDS", drive);
    Check("the reagent pre-filter ends before the tool step", end != std::string::npos);
    std::string const body = (drive == std::string::npos || end == std::string::npos)
                                 ? std::string()
                                 : source.substr(drive, end - drive);

    Check("a reagent held in full closes its request",
          Has(body, "_materialRequests.Close(name, spellId, reagentId);"));
    Check("a short reagent writes only when its request was not open",
          Has(body, "_materialRequests.Open(name, spellId, reagentId)"));
    Check("the detail is the count short, read from what the bags hold",
          Has(body, "bot->GetItemCount(reagentId, false)") &&
              Has(body, "OverseerDecisions::MaterialRequestDetail(reagentCount, have"));
    Check("the event is material-request on the recipe, keyed on the reagent item",
          Has(body, "extra.itemGuid = reagentId;") &&
              Has(body, "RecordEvent(bot, \"material-request\", spellId"));
    Check("the old count-needed detail is gone", !Has(body, "\"need \""));

    std::size_t const gate = body.find("_materialRequests.Open(");
    std::size_t const record = body.find("RecordEvent(bot, \"material-request\"");
    Check("the event is written inside the open-request gate, not before it",
          gate != std::string::npos && record != std::string::npos && gate < record);

    Check("the gate is a member of the world script, one per realm",
          Has(source, "OverseerDecisions::OpenMaterialRequests _materialRequests;"));
}

}  // namespace

int main()
{
    TheCountShort();
    OneRequestWhileOpen();
    TheAdapterUsesTheGate();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok test_craft_material_request\n");
    return EXIT_SUCCESS;
}
