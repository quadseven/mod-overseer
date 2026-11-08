/*
 * The stale combat drive (#1390) is wired where the rule needs it: on the
 * engagement clock, for every online character with a bot AI, asking
 * DecideStaleCombat and stopping combat through the core's own CombatStop
 * with a log line. The rule itself is pinned in tests/test_stale_combat.cpp;
 * this file reads src/mod_overseer.cpp as text so a refactor that drops the
 * call, or stops combat some other way, fails here.
 */

#include <cstdio>
#include <cstdlib>
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

std::string Body(std::string const& src, char const* signature)
{
    std::size_t const at = src.find(signature);
    if (at == std::string::npos)
        return std::string();
    std::size_t const end = src.find("\n    }\n", at);
    return src.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

bool Has(std::string const& hay, char const* needle)
{
    return hay.find(needle) != std::string::npos;
}

}  // namespace

int main()
{
    std::ifstream in("src/mod_overseer.cpp");
    if (!in)
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        return EXIT_FAILURE;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string const src = buf.str();

    Check("the module includes the rule", Has(src, "#include \"overseer_stale_combat.h\""));

    std::size_t const clock = src.find("            DriveStuckRevival();\n");
    std::size_t const call = src.find("            DriveStaleCombat();\n");
    Check("the drive runs on the engagement clock, after the revival drive",
          clock != std::string::npos && call != std::string::npos && clock < call &&
              call - clock < 400);

    std::string const drive = Body(src, "    void DriveStaleCombat()\n");
    Check("the drive reads every online player", Has(drive, "ObjectAccessor::GetPlayers()"));
    Check("only characters with a bot AI", Has(drive, "GET_PLAYERBOT_AI(bot)"));
    Check("an instanced map is reported to the rule",
          Has(drive, "facts.onInstanceMap = !map || map->Instanceable();"));
    Check("attackers and both kinds of combat reference count as hostiles",
          Has(drive, "bot->getAttackers()") && Has(drive, "GetPvECombatRefs()") &&
              Has(drive, "GetPvPCombatRefs()"));
    Check("a hostile counts only within the rule's reach",
          Has(drive, "OverseerDecisions::STALE_COMBAT_REACH_YARDS"));
    Check("an evading creature, or one fighting nobody, is not a hostile (#1399)",
          Has(drive, "creature->IsInEvadeMode() || creature->CanNotReachTarget()") &&
              Has(drive, "!creature->GetVictim()"));
    Check("the drive asks the rule", Has(drive, "DecideStaleCombat("));
    Check("a release stops combat through the core", Has(drive, "bot->CombatStop(true);"));
    std::size_t const stop = drive.find("bot->CombatStop(true);");
    std::size_t const line = drive.find("combat stopped so its errands can run");
    Check("the release line follows the stop and says whether combat stayed",
          stop != std::string::npos && line != std::string::npos && stop < line &&
              Has(drive, "bot->IsInCombat() ? \"yes\" : \"no\""));
    Check("a character held by a hostile is said, bounded by the rule",
          Has(drive, "SayStaleCombatHeld(now, combatSince, lastSaid)") &&
              Has(drive, "not released (#1390)"));
    Check("the books are pruned to who is in combat now", Has(drive, "seen.count(it->first)"));

    // #1411: a standoff (both sides at full health, the character unmoved) is
    // released, and each creature in it is sent home before the stop.
    Check("the drive reads the character's health and each hostile's",
          Has(drive, "bot->GetHealth() >= bot->GetMaxHealth()") &&
              Has(drive, "other->GetHealth() < other->GetMaxHealth()"));
    Check("the drive measures the move from where the stretch began",
          Has(drive, "_staleCombatAnchor[key] = bot->GetPosition();") &&
              Has(drive, "OverseerDecisions::STALE_COMBAT_STANDOFF_MOVE_YARDS"));
    Check("the rule is told whether it is a standoff",
          Has(drive, "facts.standoff = facts.hostilesInReach > 0 && allFull && !moved;"));
    std::size_t const standoff = drive.find("StaleCombatVerdict::ReleaseStandoff");
    std::size_t const evade = drive.find("creature->AI()->EnterEvadeMode();");
    std::size_t const stopAfter =
        evade == std::string::npos ? std::string::npos : drive.find("bot->CombatStop(true);", evade);
    std::size_t const standoffLine = drive.find("stood {} seconds in a standoff");
    Check("a standoff sends its creatures home, then stops combat, then says so",
          standoff != std::string::npos && evade != std::string::npos &&
              stopAfter != std::string::npos && standoffLine != std::string::npos &&
              standoff < evade && evade < stopAfter && stopAfter < standoffLine);
    Check("only a creature fighting this character is sent home",
          Has(drive, "creature->GetVictim() == bot"));
    Check("the anchor book is pruned too", Has(drive, "_staleCombatAnchor.erase(it)"));

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the stale combat drive is wired on the engagement clock\n");
    return EXIT_SUCCESS;
}
