/*
 * Zul'Farrak's End Door is driven from the guild finder run's poll (#1337).
 *
 * The decision is pinned in test_zulfarrak_end_door.cpp. This pins the
 * adapter: the poll of a running guild run asks it before the stall ladder,
 * reads the core's own instance data (DATA_PYRAMID, DATA_END_DOOR), sends a
 * living Weegli with the core's ACTION_DESTROY_GATES, and opens the door the
 * way his barrel's SmartAI does (GO_STATE_ACTIVE_ALTERNATIVE, end door DONE).
 */

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

std::string ReadSource()
{
    std::ifstream file("src/mod_overseer.cpp");
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

std::string Between(std::string const& source, std::string const& start,
                    std::string const& end)
{
    std::string::size_type first = source.find(start);
    if (first == std::string::npos)
        return {};
    first += start.size();
    std::string::size_type last = source.find(end, first);
    return last == std::string::npos ? std::string() : source.substr(first, last - first);
}

bool Has(std::string const& text, char const* needle)
{
    return text.find(needle) != std::string::npos;
}
}  // namespace

int main()
{
    std::string const source = ReadSource();
    Check("the adapter source was read", !source.empty());

    std::string const step = Between(source, "void StepZulFarrakEndDoor(GuildRun& run, std::time_t now)",
                                     "bool StepGuildRunStall(");
    Check("StepZulFarrakEndDoor exists", !step.empty());
    Check("it acts only on Zul'Farrak", Has(step, "run.mapId != OverseerDecisions::ZF_MAP_ID"));
    Check("it asks the decision", Has(step, "OverseerDecisions::ZulFarrakEndDoorNext(facts)"));
    Check("it reads DATA_PYRAMID off the instance script",
          Has(step, "script->GetData(OverseerDecisions::ZF_DATA_PYRAMID)"));
    Check("it reads DATA_END_DOOR off the instance script",
          Has(step, "script->GetData(OverseerDecisions::ZF_DATA_END_DOOR)"));
    Check("it finds the End Door near the tank",
          Has(step, "FindNearestGameObject(OverseerDecisions::ZF_END_DOOR_ENTRY"));
    Check("a shut door is GO_STATE_READY", Has(step, "GetGoState() == GO_STATE_READY"));
    Check("it sends Weegli with the core's action",
          Has(step, "DoAction(OverseerDecisions::ZF_ACTION_DESTROY_GATES)"));
    Check("it opens the door as the barrel does",
          Has(step, "door->SetGoState(GO_STATE_ACTIVE_ALTERNATIVE)"));
    Check("and records the end door DONE",
          Has(step, "script->SetData(OverseerDecisions::ZF_DATA_END_DOOR,"));

    std::string const poll = Between(source, "bool DriveGuildRun(GuildRun& run, std::time_t now)",
                                     "// LEAVING: the living were sent out");
    std::string::size_type const door = poll.find("StepZulFarrakEndDoor(run, now);");
    std::string::size_type const ladder = poll.find("StepGuildRunStall(run, poll, regrouping, now, why)");
    Check("the guild run poll calls it", door != std::string::npos);
    Check("before the stall ladder", door != std::string::npos && ladder != std::string::npos &&
                                         door < ladder);

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("the Zul'Farrak end door wiring is in place\n");
    return 0;
}
