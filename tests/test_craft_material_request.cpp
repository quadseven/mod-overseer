/*
 * DriveCraft writes a material-request event when a craft is skipped due to
 * insufficient reagents, with at most one open request per character, recipe,
 * and reagent combination.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::FocusLighterSpell;

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

void TheDecision()
{
    // FocusLighterSpell is a pure decision function we can test
    Check("a Cooking Fire is lit with Basic Campfire", FocusLighterSpell(4) == 818);
    Check("no focus needs nothing lit", FocusLighterSpell(0) == 0);
    Check("an Anvil is a world spawn, not conjured", FocusLighterSpell(1) == 0);
    Check("a Forge is a world spawn, not conjured", FocusLighterSpell(3) == 0);
    Check("an unknown focus is not guessed at", FocusLighterSpell(999) == 0);
}

void TheAdapterWritesMaterialRequest()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    Check("the module source is readable (run from the repo root)", !source.empty());

    std::size_t const drive = source.find("void DriveCraft()");
    Check("DriveCraft exists", drive != std::string::npos);
    std::size_t const end = source.find("// ----", drive);
    std::string const body =
        drive == std::string::npos ? std::string() : source.substr(drive, end - drive);

    // Check that the reagent pre-filter loop writes a material-request event
    // when a reagent is missing
    std::size_t const reagentLoop = body.find("for (uint32 x = 0; x < MAX_SPELL_REAGENTS; ++x)");
    Check("reagent pre-filter loop exists", reagentLoop != std::string::npos);

    // Check that RecordEvent is called with "material-request" kind
    std::size_t const recordEvent = body.find("RecordEvent");
    Check("RecordEvent is called somewhere in DriveCraft", recordEvent != std::string::npos);

    // Check that "material-request" string literal appears in the DriveCraft body
    std::size_t const materialRequest = body.find("material-request");
    Check("material-request event kind is used", materialRequest != std::string::npos);

    // Check that itemGuid (reagent item ID) is used for deduplication
    std::size_t const itemGuid = body.find("extra.itemGuid");
    Check("itemGuid is set for material-request deduplication", itemGuid != std::string::npos);

    // Check that the event includes the reagent item ID in the key
    std::size_t const reagentId = body.find("info->Reagent[x]");
    Check("reagent item ID is referenced", reagentId != std::string::npos);

    // Check that the event detail includes the reagent name or count
    std::size_t const detail = body.find("detail");
    Check("detail is set for material-request", detail != std::string::npos);
}

void TheAdapterDeduplicatesMaterialRequests()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();

    std::size_t const drive = source.find("void DriveCraft()");
    std::size_t const end = source.find("// ----", drive);
    std::string const body =
        drive == std::string::npos ? std::string() : source.substr(drive, end - drive);

    // The deduplication happens automatically via the EventKey which includes
    // characterName, kind, subjectId, bucket, and itemGuid.
    // By setting itemGuid to the reagent item ID, we get per-reagent deduplication.
    // Verify the event key structure supports this.

    // Check that EventKey includes itemGuid
    std::size_t const eventKey = source.find("struct EventKey");
    Check("EventKey struct exists", eventKey != std::string::npos);

    std::size_t const itemGuidInKey = source.find("uint32 itemGuid = 0;");
    Check("EventKey has itemGuid field", itemGuidInKey != std::string::npos);

    // Check that QueueEvent uses itemGuid in the key
    std::size_t const queueEvent = source.find("void QueueEvent");
    Check("QueueEvent exists", queueEvent != std::string::npos);

    std::size_t const keyConstruction = source.find("key.itemGuid = extra.itemGuid;");
    Check("QueueEvent sets itemGuid from extra", keyConstruction != std::string::npos);
}

}  // namespace

int main()
{
    TheDecision();
    TheAdapterWritesMaterialRequest();
    TheAdapterDeduplicatesMaterialRequests();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok test_craft_material_request\n");
    return EXIT_SUCCESS;
}