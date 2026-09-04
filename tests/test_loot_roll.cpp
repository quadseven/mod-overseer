/*
 * The roster must answer group loot through the stock playerbot action.
 *
 * This is a source contract because LootRollAction depends on the full core
 * and cannot be linked by the pure decision test harness. Keeping the check
 * here still pins the important seam: the module invokes the upstream policy
 * directly, and does so before its narrower gear arbitration.
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

void RosterUsesStockRollPolicy()
{
    std::string const source = ReadModule();
    Expect(source.find("#include \"LootRollAction.h\"") != std::string::npos,
           "the stock loot action is available to the roster drive");
    Expect(source.find("LootRollAction action(botAI);") != std::string::npos,
           "the roster constructs the stock loot action");
    Expect(source.find("action.Execute(Event(\"overseer loot roll\"") != std::string::npos,
           "the roster executes the stock loot action directly");

    std::size_t const answer = source.find("AnswerOpenRolls(members);");
    std::size_t const custom = source.find("VoteOnOpenRolls(group, members);");
    Expect(answer != std::string::npos && custom != std::string::npos && answer < custom,
           "stock policy runs before the narrower gear ballot");
}

} // namespace

int main()
{
    RosterUsesStockRollPolicy();
    return 0;
}
