/*
 * Adapter wiring for the guild-seat unlearn-skill command (mod-overseer#1556).
 */
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int Fail(char const* what)
{
    std::cerr << "FAIL " << what << "\n";
    return EXIT_FAILURE;
}

std::string Read(char const* path)
{
    std::ifstream input(path);
    if (!input)
        return {};
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

std::string Between(std::string const& text, char const* start, char const* end)
{
    std::size_t const first = text.find(start);
    if (first == std::string::npos)
        return {};
    std::size_t const last = text.find(end, first + std::string(start).size());
    if (last == std::string::npos)
        return {};
    return text.substr(first, last - first);
}
}  // namespace

int main()
{
    std::string const source = Read("src/mod_overseer.cpp");
    std::string const readme = Read("README.md");
    if (source.empty() || readme.empty())
        return Fail("cannot read module source and README from the repository root");

    if (source.find("kind == \"cast\" && OverseerDecisions::IsUnlearnSkillRow(command)") ==
            std::string::npos ||
        source.find("DoUnlearnSkill(player, command, status, rowResult)") == std::string::npos)
        return Fail("cast rows must route the unlearn-skill verb to its handler");

    std::string const handler = Between(source, "static char const* DoUnlearnSkill(",
                                        "static char const* DoLearnRecipe(");
    if (handler.empty())
        return Fail("cannot find DoUnlearnSkill before DoLearnRecipe");
    if (handler.find("DecideUnlearnSkill(") == std::string::npos ||
        handler.find("InNaturalGuild(who)") == std::string::npos ||
        handler.find("OnRoster(who->GetName())") == std::string::npos)
        return Fail("the handler must enforce natural-guild and non-roster eligibility");
    if (handler.find("eventKind = refused ? \"unlearn_refused\" : \"unlearn\"") ==
            std::string::npos ||
        handler.find("QueueEvent(who, eventKind") == std::string::npos ||
        handler.find("RecordEvent(") != std::string::npos ||
        handler.find("LOG_WARN(") == std::string::npos ||
        handler.find("LOG_INFO(") == std::string::npos)
        return Fail("refusals and outcomes must be logged and queued for non-roster events");
    if (handler.find("finish(\"unlearned\"") == std::string::npos ||
        handler.find("live_value") == std::string::npos)
        return Fail("the command result must report unlearned and the live skill value");

    std::size_t const set = handler.find("who->SetSkill(static_cast<uint16>(request.skill), 0, 0, 0)");
    std::size_t const readback = handler.find("who->HasSkill(request.skill)", set);
    if (set == std::string::npos || readback == std::string::npos || readback < set)
        return Fail("SetSkill must be followed by a HasSkill read-back");

    if (readme.find("unlearn-skill skill:<id> max:<ceiling>") == std::string::npos)
        return Fail("README must list the new command grammar");

    std::cout << "test_unlearn_skill_source: all cases passed\n";
    return EXIT_SUCCESS;
}
