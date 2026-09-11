/*
 * Group loot is a deadline-driven reaction. Keep the integration seam pinned
 * in a source contract because LootRollAction depends on the full core and
 * cannot be linked by the pure decision harness.
 */

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

void Expect(bool condition, char const* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

std::string ReadModule()
{
    std::ifstream source("src/mod_overseer.cpp");
    std::ostringstream contents;
    contents << source.rdbuf();
    return contents.str();
}

void RosterAnswersOpenRolls()
{
    std::string const source = ReadModule();
    Expect(source.find("#include \"LootRollAction.h\"") != std::string::npos,
           "the stock loot action is available to the roster drive");
    Expect(source.find("void AnswerOpenRolls(std::vector<GearMember> const& members)") !=
               std::string::npos,
           "the roster has a dedicated open-roll reaction");
    Expect(source.find("LootRollAction action(botAI);") != std::string::npos,
           "the roster constructs the stock loot action");
    Expect(source.find("action.Execute(Event(\"overseer loot roll\"") != std::string::npos,
           "the roster executes the stock loot action directly");

    std::size_t const answer = source.find("AnswerOpenRolls(members);");
    std::size_t const sweep = source.find("SweepGear(member.bot, member.who);");
    Expect(answer != std::string::npos && sweep != std::string::npos && answer < sweep,
           "the deadline reaction runs before gear arbitration");
}

} // namespace

int main()
{
    RosterAnswersOpenRolls();
    return 0;
}
