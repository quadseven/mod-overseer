/*
 * A worn Blacksmithing recipe needs an Anvil and a Blacksmith Hammer (#524).
 *
 * Runed Copper Belt (2666) and every worn recipe after it carry
 * RequiresSpellFocus 1 (Anvil) and RequiredTotemCategoryID 162 (Blacksmith
 * Hammer). The hammer is a tool: the core's CheckCast is satisfied by one in
 * the bags. The operator's decision is that it is bought, kept in the
 * crafter's own bank and taken out to craft. So the craft drive:
 *
 *   - takes the hammer out of the bank when a banker is in reach, walks a
 *     job 'craft' crafter to a banker when not, and asks for one to be bought
 *     when the crafter has none at all;
 *   - walks a job 'craft' crafter refused for an Anvil to the nearest Anvil
 *     spawn within a town's walk, and stands when already at one;
 *   - puts the hammer back in the bank at the next banker once no standing
 *     craft errand needs it.
 *
 * The decisions compile against the pure decision file and nothing from
 * AzerothCore; the wiring is read from the adapter source as text (run from
 * the repo root).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace OverseerDecisions;

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

void TheIds()
{
    Check("the Anvil is spell focus 1", SPELL_FOCUS_ANVIL == 1);
    Check("the Blacksmith Hammer is tool category 162", TOTEM_CATEGORY_BLACKSMITH_HAMMER == 162);
    Check("tool category 162 is bought as item 5956", ToolItemForTotemCategory(162) == 5956);
    Check("no tool category names no item", ToolItemForTotemCategory(0) == 0);
    Check("an unknown tool category is not guessed at", ToolItemForTotemCategory(165) == 0);
    Check("an Anvil is walked to", FocusIsWalkedTo(1));
    Check("a Cooking Fire is lit, not walked to", !FocusIsWalkedTo(4));
    Check("a Forge is not walked to by this drive", !FocusIsWalkedTo(3));
    Check("the old misreading (10 is Machine Shop) is not an Anvil", !FocusIsWalkedTo(10));
    Check("no focus is not walked to", !FocusIsWalkedTo(0));
}

void TheTool()
{
    Check("a recipe with no tool is cast",
          CraftToolStepFor(CraftToolPlace::NotNeeded, false, true) == CraftToolStep::Cast);
    Check("a carried hammer is cast with",
          CraftToolStepFor(CraftToolPlace::Carried, false, false) == CraftToolStep::Cast);
    Check("a banked hammer with a banker here is withdrawn",
          CraftToolStepFor(CraftToolPlace::Banked, true, true) == CraftToolStep::Withdraw);
    Check("a banked hammer is withdrawn between runs too, where a banker stands",
          CraftToolStepFor(CraftToolPlace::Banked, true, false) == CraftToolStep::Withdraw);
    Check("a banked hammer and no banker walks a craft-job crafter to one",
          CraftToolStepFor(CraftToolPlace::Banked, false, true) == CraftToolStep::WalkToBanker);
    Check("a campaign family is never walked off for a hammer",
          CraftToolStepFor(CraftToolPlace::Banked, false, false) == CraftToolStep::Wait);
    Check("no hammer at all is requested, never conjured",
          CraftToolStepFor(CraftToolPlace::Missing, true, true) == CraftToolStep::Request);
    Check("the steps are named", std::string(CraftToolStepWord(CraftToolStep::Withdraw)) == "withdraw");

    Check("a hammer no errand needs goes back at a banker", CraftToolGoesBack(true, false, true, true));
    Check("a hammer an errand still needs stays in the bags", !CraftToolGoesBack(true, true, true, true));
    Check("nobody is walked to a banker to put it back", !CraftToolGoesBack(true, false, false, true));
    Check("not while unable to bank", !CraftToolGoesBack(true, false, true, false));
    Check("a hammer that is not in the bags is left alone", !CraftToolGoesBack(false, false, true, true));
}

void TheAnvil()
{
    std::vector<FocusSpawn> const spawns = {
        {0, 1000.f, 1000.f, 50.f},   // same map, 300 yards
        {0, 750.f, 700.f, 50.f},     // same map, 50 yards
        {1, 700.f, 700.f, 50.f},     // another map, 0 yards
        {0, 2000.f, 2000.f, 50.f},   // same map, too far
    };
    Check("the nearest same-map Anvil is chosen",
          NearestFocusSpawn(spawns, 0, 700.f, 700.f, CRAFT_FOCUS_WALK_LIMIT_YARDS) == 1);
    Check("an Anvil on another map is never chosen",
          NearestFocusSpawn(spawns, 1, 750.f, 750.f, CRAFT_FOCUS_WALK_LIMIT_YARDS) == 2 &&
              NearestFocusSpawn(spawns, 530, 700.f, 700.f, CRAFT_FOCUS_WALK_LIMIT_YARDS) == -1);
    Check("an Anvil past the walk limit is not chosen",
          NearestFocusSpawn(spawns, 0, 2000.f, 1500.f, CRAFT_FOCUS_WALK_LIMIT_YARDS) == -1);
    Check("no spawns, no choice", NearestFocusSpawn({}, 0, 0.f, 0.f, 400.f) == -1);

    Check("a craft-job crafter far from an Anvil walks",
          CraftFocusStepFor(true, true, 50.f) == CraftFocusStep::Walk);
    Check("already at an Anvil it stands rather than walking again",
          CraftFocusStepFor(true, true, 3.f) == CraftFocusStep::Stand);
    Check("no Anvil in a town's walk is said, not walked",
          CraftFocusStepFor(true, true, -1.f) == CraftFocusStep::NoneNear);
    Check("a campaign family is never walked to an Anvil",
          CraftFocusStepFor(true, false, 50.f) == CraftFocusStep::Stand);
    Check("a focus that is not walked to stands",
          CraftFocusStepFor(false, true, 50.f) == CraftFocusStep::Stand);

    std::string const aim = FocusSpawnAim({0, -4917.6f, -955.2f, 501.5f});
    Check("the aim is an at: place in whole yards", aim == "at:0:-4918,-955,502");
    Check("and fits the 32 character travel column",
          FocusSpawnAim({571, -12345.6f, -12345.6f, -1234.5f}).size() <= 32);
}

void TheAdapterIsWired()
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

    std::size_t const tool = body.find("info->TotemCategory[");
    std::size_t const step = body.find("OverseerDecisions::CraftToolStepFor(");
    std::size_t const cast = body.find("bot->CastSpell(bot, spellId, false)");
    Check("DriveCraft reads the recipe's tool categories", tool != std::string::npos);
    Check("and decides the tool step before the recipe is cast",
          step != std::string::npos && step > tool && cast != std::string::npos && step < cast);
    Check("the hammer is taken out through the bank verb",
          body.find("DoBank(bot, \"withdraw guid:\"") != std::string::npos);
    Check("a banker walk is a town-trip claim on the travel book",
          body.find("_travelAims.Claim(name, \"banker\", OverseerDecisions::TravelOwner::TownTrip)") !=
              std::string::npos);
    Check("a missing hammer is a material request for the tool item",
          body.find("OverseerDecisions::ToolItemForTotemCategory(") != std::string::npos);

    std::size_t const refusal = body.find("if (result == SPELL_FAILED_REQUIRES_SPELL_FOCUS)");
    std::size_t const walked = body.find("OverseerDecisions::CraftFocusStepFor(");
    Check("a focus refusal asks whether to walk to an Anvil",
          refusal != std::string::npos && walked != std::string::npos && walked > refusal);
    Check("the Anvil walk is a town-trip claim of an at: place",
          body.find("_travelAims.Claim(name, aim, OverseerDecisions::TravelOwner::TownTrip)") !=
              std::string::npos);

    Check("the Anvil index is read from gameobject spawns",
          source.find("sObjectMgr->GetAllGOData()") != std::string::npos &&
              source.find("GAMEOBJECT_TYPE_SPELL_FOCUS") != std::string::npos &&
              source.find("spellFocus.focusId") != std::string::npos);
    Check("the hammer goes back to the bank through the bank verb",
          source.find("void ReturnCraftTools()") != std::string::npos &&
              source.find("OverseerDecisions::CraftToolGoesBack(") != std::string::npos &&
              source.find("DoBank(bot, \"deposit guid:\"") != std::string::npos);
    Check("and that pass runs on the craft poll",
          source.find("DriveCraft();\n            ReturnCraftTools();") != std::string::npos);
    Check("no weapon swap: the hammer is never equipped by the craft drive",
          body.find("SwapItem") == std::string::npos && body.find("CanEquipItem") == std::string::npos);
}

}  // namespace

int main()
{
    TheIds();
    TheTool();
    TheAnvil();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok test_craft_anvil_hammer\n");
    return 0;
}
