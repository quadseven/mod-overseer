#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int Fail(char const* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}
}  // namespace

int main()
{
    std::ifstream patch("patches/mod-playerbots/0026-grind-resource-rest-before-pull.patch");
    if (!patch)
        return Fail("cannot read the grind resource hold patch");

    std::string added;
    std::string line;
    while (std::getline(patch, line))
    {
        if (line.rfind("+++", 0) == 0)
            continue;
        if (!line.empty() && line[0] == '+')
        {
            added.append(line.substr(1));
            added.push_back('\n');
        }
    }

    char const* const required[] = {
        "ShouldHoldGrindPull(resourceState)",
        "AI_VALUE2(uint32, \"item count\", \"water\") > 0",
        "AI_VALUE2(uint32, \"item count\", \"food\") > 0",
        "creators[\"grind resource hold\"]",
        "creators[\"rest before grinding\"]",
        "NextAction(\"rest before grinding\", 100.0f)",
        "bot->StopMoving();",
        "bot->SetStandState(UNIT_STAND_STATE_SIT);",
    };
    for (char const* expected : required)
    {
        if (added.find(expected) == std::string::npos)
            return Fail("patch does not wire the tested resource hold into grinding");
    }

    if (added.find("HasCheat(BotCheatMask::food)") != std::string::npos ||
        added.find("AddAura(25990") != std::string::npos)
        return Fail("resource hold must not use the food cheat");

    std::cout << "ok\n";
    return EXIT_SUCCESS;
}
