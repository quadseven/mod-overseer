#include "overseer_decisions.h"
#include <cassert>

int main()
{
    using namespace OverseerDecisions;
    auto const legacy = ParseGuildFinderRequest(
        "finder-run deadmines Healer First Second Third", "Carrier");
    assert(legacy.error == GuildFinderRefusal::None);
    auto const carried = ParseGuildFinderRequest(
        "finder-run deadmines Healer First Second Third carry First", "Carrier");
    assert(carried.error == GuildFinderRefusal::None);
    assert(carried.carried == "First");
    assert(legacy.carried.empty());
    // Deadmines carrier (level 40) and passenger (level 26): levels select
    // the formation upstream; this seam receives the resolved seats only.
    // The tank retains the skull-first plan; the passenger cannot engage.
    assert(GuildRunSeatTactics(GuildSeat::Tank, false) ==
           std::vector<std::string>{"mark rti"});
    assert(ParseGuildFinderRequest(
        "finder-run deadmines Healer First Second Third carry Healer", "Carrier").error ==
           GuildFinderRefusal::None);
    assert(GuildRunSeatTactics(GuildSeat::Damage, true) ==
           std::vector<std::string>{"passive"});
    for (auto seat : {GuildSeat::Tank, GuildSeat::Healer, GuildSeat::Damage})
        assert(GuildRunSeatTactics(seat, false) == GuildRunSeatTactics(seat));
    for (auto const* tail : {"carry Carrier", "carry Absent", "carry First carry Second", "carry"})
        assert(ParseGuildFinderRequest(std::string("finder-run deadmines Healer First Second Third ") + tail,
                                       "Carrier").error != GuildFinderRefusal::None);
    auto mixed = ParseGuildFinderRequest(
        "finder-run deadmines Healer First Second Third carry First pug First pug Second", "Carrier");
    assert(mixed.error == GuildFinderRefusal::None && mixed.pugs.size() == 2);
}
