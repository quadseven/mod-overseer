// DriveTravel and DriveFamilyQuests share the cross-map errand cooldown.
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
bool Ordered(std::string const& source, std::initializer_list<char const*> markers)
{
    std::size_t at = 0;
    for (char const* marker : markers)
    {
        at = source.find(marker, at);
        if (at == std::string::npos)
        {
            std::cerr << "Missing or out of order: " << marker << '\n';
            return false;
        }
        at += std::char_traits<char>::length(marker);
    }
    return true;
}
}

int main()
{
    std::ifstream file("src/mod_overseer.cpp");
    std::string const source((std::istreambuf_iterator<char>(file)), {});

    // The rewrite must be ignored before it can take the leader's intent.
    if (!Ordered(source, {"    void DriveTravel()", "CrossMapAimCoolingOff(name, target,",
                          "continue;", "AskForLeader(name, kind, owner, about",
                          "!ResolveTravelTarget(bot, target, entry, pos, wantSkill, &said)",
                          "FindCrossMapTravelSpawn(", "RecordCrossMapTravelRefusal(",
                          "_travelAims.ReleaseIfCurrent(name, target,", "continue;",
                          "ChangeToWanderNpc(moverEntry, aimAt)"}))
        return EXIT_FAILURE;

    // Both an explicit council aim and the leader's own log can re-pick it.
    if (!Ordered(source, {"    void DriveFamilyQuests(", "QuestCoolingOff(aim)",
                          "DriveChosenQuest(name, bot, botAI, aim,", "QuestCoolingOff(working)",
                          "QuestCoolingOff(questId)", "candidates.push_back({questId"}) ||
        !Ordered(source, {"    bool TravelHoldsTheWheel(",
                          "!CrossMapAimCoolingOff(name, travelTarget,",
                          "return _travelAims.WithinHandbackGrace(name);"}))
        return EXIT_FAILURE;

    // The Watch feed reads stored steps, not worldserver log messages.
    if (!Ordered(source, {"    void RecordCrossMapTravelRefusal(", "UnresolvedSpawnRefusal(",
                          "Retry after ", "LOG_INFO(", "name, step", "INSERT INTO overseer_now_step",
                          "'intent_end'", "Esc(name), Esc(step)"}))
        return EXIT_FAILURE;

    if (!Ordered(source, {"    bool ResolveTravelTargetOnMap(",
                          "data->mapid != mapId", "exactSpawn = ReadTravelSpawn(",
                          "consider(*exactSpawn)", "ChooseTravelTarget(candidates)",
                          "outPos = WorldPosition(chosen.mapId"}))
        return EXIT_FAILURE;

    std::cout << "cross-map travel refusal wiring passed\n";
    return EXIT_SUCCESS;
}
