/*
 * The `auras` probe (2026-09-29).
 *
 * Asked "why does nobody carry a buff", the only way to answer from outside
 * the worldserver was a screenshot of a party frame, which cannot tell a buff
 * that was never cast from a buff the client hides. character_aura is saved
 * on logout, so it is a photograph too. The probe asks the living Player what
 * it carries, the way `spells` and `strategies` do, and mutates nothing.
 *
 * Compiled against src/overseer_decisions.cpp; the last part reads
 * src/mod_overseer.cpp (run from the repo root) to pin the wiring.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

bool Has(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

void ANobodyHasNoAuras()
{
    std::string const json = OverseerDecisions::AuraProbeJson({});
    Check("no auras is an empty list, not a missing key", Has(json, "\"auras\":[]"));
    Check("and says zero", Has(json, "\"count\":0") && Has(json, "\"positive\":0"));
}

void AuraFactsAreReported()
{
    OverseerDecisions::AuraFact fortitude;
    fortitude.spellId = 1245;
    fortitude.stacks = 1;
    fortitude.remainingMs = 1800000;
    fortitude.positive = true;
    fortitude.caster = "Ugga";
    OverseerDecisions::AuraFact debuff;
    debuff.spellId = 15007;
    debuff.stacks = 1;
    debuff.remainingMs = -1;
    debuff.positive = false;
    debuff.caster = "";
    std::string const json = OverseerDecisions::AuraProbeJson({fortitude, debuff});
    Check("the spell id is carried", Has(json, "\"spell\":1245") && Has(json, "\"spell\":15007"));
    Check("the caster is carried", Has(json, "\"caster\":\"Ugga\""));
    Check("the remaining time is carried, -1 for none",
          Has(json, "\"remaining_ms\":1800000") && Has(json, "\"remaining_ms\":-1"));
    Check("counts split buffs from debuffs",
          Has(json, "\"count\":2") && Has(json, "\"positive\":1"));
    Check("a debuff says it is not positive", Has(json, "\"positive\":false"));
}

void ACasterNameIsEscaped()
{
    OverseerDecisions::AuraFact odd;
    odd.spellId = 1;
    odd.caster = "a\"b\\c";
    std::string const json = OverseerDecisions::AuraProbeJson({odd});
    Check("quotes and backslashes in a name cannot break the json",
          Has(json, "\"caster\":\"a\\\"b\\\\c\""));
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream text;
    text << in.rdbuf();
    std::string const source = text.str();
    if (source.empty())
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        ++failures;
        return;
    }
    Check("DoProbe answers auras",
          Has(source, "else if (what == \"auras\")\n            out = ProbeAuras(bot);"));
    Check("the unknown-probe message lists it", Has(source, "|bags|auras|meter)"));
    std::size_t const at = source.find("static std::string ProbeAuras(Player* bot)");
    Check("ProbeAuras exists", at != std::string::npos);
    std::size_t const end = source.find("static std::string ProbeBags(Player* bot)", at);
    std::string const body = at == std::string::npos ? "" : source.substr(at, end - at);
    Check("it reads the applied auras and formats them in the decisions module",
          Has(body, "GetAppliedAuras()") && Has(body, "AuraProbeJson("));
    Check("and casts, applies and removes nothing",
          !Has(body, "CastSpell") && !Has(body, "AddAura") && !Has(body, "RemoveAura"));
}

}  // namespace

int main()
{
    ANobodyHasNoAuras();
    AuraFactsAreReported();
    ACasterNameIsEscaped();
    TheAdapterIsWired();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_aura_probe: all passed\n");
    return 0;
}
