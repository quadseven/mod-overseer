/*
 * The wiring the lone-crafter rule (#1208) depends on, read as text from the
 * adapter: deaths on job 'craft' are recorded where the death rows are
 * written, the rule is asked before the craft drive on the same clock, and the
 * craft drive skips a crafter the rule holds. The rule itself is
 * test_craft_lone_deaths.cpp.
 *
 * Reads source as text (run from the repo root); needs no link.
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

} // namespace

int main()
{
    std::ifstream in("src/mod_overseer.cpp");
    if (!in)
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        return 1;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string const src = buf.str();

    std::string const flush = Body(src, "    void FlushDeaths()\n");
    Check("FlushDeaths records every craft-job death",
          Has(flush, "if (d.job == \"craft\")") && Has(flush, "_craftLoneDeaths[d.characterName].push_back"));
    Check("the recorded death keeps whether it was grouped", Has(flush, "death.grouped = d.grouped != 0;"));

    std::string const drive = Body(src, "    void DriveCraftLoneDeaths()\n");
    Check("the drive asks the decision", Has(drive, "OverseerDecisions::DecideCraftLoneDeaths(facts)"));
    Check("the drive counts deaths in the zone it stands in now",
          Has(drive, "bot->GetMapId(), bot->GetZoneId(), now"));
    Check("the drive hearths through DoHearth", Has(drive, "DoHearth(bot, \"use\""));
    Check("a hearth DoHearth refuses is named, once per refusal",
          Has(drive, "std::string const refusal =") &&
              Has(drive, "_craftLoneHearthRefused[name] != refusal") &&
              Has(drive, "cannot hearth out of zone"));
    Check("the drive names the reason in a log line", Has(drive, "CraftLoneDeathStepWord(step)") && Has(drive, "(#1208)"));

    // THE PROOF LINE FOLLOWS THE CAST (#1225). "is not left idle there -
    // hearthing to its bind" is the line a roll is read by, so it is written
    // only once DoHearth has returned with no refusal; a refused cast says its
    // reason instead and the proof line waits for the poll whose cast starts.
    std::size_t const cast = drive.find("DoHearth(bot, \"use\"");
    std::size_t const proof = drive.find("is not left idle there");
    Check("the proof line is logged after DoHearth, not before it",
          cast != std::string::npos && proof != std::string::npos && cast < proof);
    Check("the proof line is guarded by a cast with no refusal",
          Has(drive, "if (!refusal.empty())") && Has(drive, "_craftLoneHearthRefused.erase(name);\n            }\n            auto const said"));

    std::size_t const lone = src.find("            DriveCraftLoneDeaths();\n");
    std::size_t const craft = src.find("            DriveCraft();\n");
    Check("the rule is asked before the craft drive on its clock",
          lone != std::string::npos && craft != std::string::npos && lone < craft);

    std::string const craftBody = Body(src, "    void DriveCraft()\n");
    Check("the craft drive skips a held crafter", Has(craftBody, "if (_craftLoneHeld.count(name))"));

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok - the lone-crafter rule is wired into the craft drive\n");
    return 0;
}
