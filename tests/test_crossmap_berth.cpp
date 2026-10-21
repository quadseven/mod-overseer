/*
 * Cross-map berth reading: picking the right transport and validating its berth.
 *
 * A member stranded on another continent (quadseven/mod-overseer#274) needs to
 * find a transport that serves both maps, read its stop-frame berth (not the
 * mooring), and confirm the berth is not guarded by hostile spawns.
 *
 * The pure function takes a transport catalogue, origin/destination maps, the
 * leader's position, hostile spawns at the berth, and the member's level and
 * threat gap. It returns the chosen transport, its berth coordinates, and
 * whether the berth is guarded.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdio>
#include <vector>

using OverseerDecisions::TransportBerthInfo;
using OverseerDecisions::CrossMapStop;
using OverseerDecisions::CrossingCrewMember;
using OverseerDecisions::CrossMapBerthFacts;
using OverseerDecisions::CrossMapBerthResult;
using OverseerDecisions::ReadCrossMapBerth;
using OverseerDecisions::PlaceHostile;
using OverseerDecisions::PlaceDangerFacts;
using OverseerDecisions::PlaceDanger;
using OverseerDecisions::JudgePlaceDanger;
using OverseerDecisions::PlaceDangerReason;

namespace
{

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckInt(char const* what, long got, long want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %ld, wanted %ld\n", what, got, want);
    ++failures;
}

void CheckFloat(char const* what, float got, float want, float eps = 0.01f)
{
    if (std::fabs(got - want) <= eps)
        return;
    std::printf("FAIL %s: got %.2f, wanted %.2f\n", what, got, want);
    ++failures;
}

void CheckString(char const* what, std::string const& got, char const* want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want);
    ++failures;
}

// Build a minimal transport with one stop on each map, both with known berths.
// The caller must ensure the returned CrossMapBerthFacts references remain valid.
CrossMapBerthFacts MakeFacts(std::vector<TransportBerthInfo> transports,
                             std::vector<PlaceHostile> hostiles,
                             uint32_t originMap, uint32_t destinationMap,
                             float leaderX, float leaderY,
                             uint32_t memberLevel, uint32_t threatLevelGap)
{
    // These must outlive the returned CrossMapBerthFacts.
    static std::vector<TransportBerthInfo> staticTransports;
    static std::vector<PlaceHostile> staticHostiles;

    staticTransports = std::move(transports);
    staticHostiles = std::move(hostiles);

    CrossMapBerthFacts f;
    f.transports = staticTransports;
    f.originMap = originMap;
    f.destinationMap = destinationMap;
    f.leaderX = leaderX;
    f.leaderY = leaderY;
    f.hostilesAtBerth = staticHostiles;
    f.memberLevel = memberLevel;
    f.threatLevelGap = threatLevelGap;
    return f;
}

// Build a minimal transport with one stop on each map, both with known berths.
TransportBerthInfo Transport(uint32_t entry, char const* name, uint32_t pathMs,
                             uint32_t originMap, float ox, float oy, float oz,
                             uint32_t destMap, float dx, float dy, float dz)
{
    TransportBerthInfo t;
    t.entry = entry;
    t.name = name;
    t.pathTimeMs = pathMs;
    t.mapsUsed.insert(originMap);
    t.mapsUsed.insert(destMap);

    CrossMapStop from;
    from.mapId = originMap;
    from.arriveMs = 0;
    from.departMs = 60000;  // departs at 1 minute
    from.berthKnown = true;
    from.berthX = ox;
    from.berthY = oy;
    from.berthZ = oz;
    t.stops.push_back(from);

    CrossMapStop to;
    to.mapId = destMap;
    to.arriveMs = 300000;  // arrives at 5 minutes
    to.departMs = 360000;
    to.berthKnown = true;
    to.berthX = dx;
    to.berthY = dy;
    to.berthZ = dz;
    t.stops.push_back(to);

    return t;
}

// Hostile at the given level.
PlaceHostile Hostile(uint32_t level, uint32_t entry = 12345)
{
    PlaceHostile h;
    h.level = level;
    h.entry = entry;
    return h;
}

PlaceDanger GuardedBy(uint32_t level)
{
    PlaceDangerFacts facts;
    facts.memberLevel = 10;
    facts.threatLevelGap = 5;
    facts.hostiles = {Hostile(level)};
    facts.deathsNear = 0;
    facts.repeatDeaths = 2;
    facts.killerEntry = 0;
    return JudgePlaceDanger(facts);
}

// A member on map 0 wants to reach map 1 (Kalimdor).
// Leader is at (-8000, -500) on map 0.
// The boat from Ratchet to Booty Bay (map 0 -> map 1):
//   - berth at Ratchet: (-1005, -3845) on map 0
//   - landing at Booty Bay: (-1005, -3845) on map 1
CrossMapBerthFacts BasicFacts()
{
    // Stack variables that outlive the returned CrossMapBerthFacts.
    std::vector<TransportBerthInfo> transports = {
        Transport(176310, "Ratchet Booty Bay Boat", 420000,
                  0, -1005, -3845, 6.f,   // Ratchet berth
                  1, -1005, -3845, 6.f)   // Booty Bay landing
    };
    std::vector<PlaceHostile> hostiles;
    return MakeFacts(std::move(transports), std::move(hostiles),
                     0, 1, -8000.f, -500.f, 10, 5);
}

void NoTransportFound()
{
    // Empty catalogue: no transport serves both maps.
    std::vector<TransportBerthInfo> empty;
    CrossMapBerthResult r = ReadCrossMapBerth(MakeFacts(empty, {}, 0, 1, 0, 0, 10, 5));
    Check("no transport -> transportFound false", r.transportFound, false);
    Check("no transport -> berthKnown false", r.berthKnown, false);
}

void TransportNotServingBothMaps()
{
    // Transport only on map 0 (zeppelin that never leaves EK).
    std::vector<TransportBerthInfo> transports = {
        Transport(176495, "The Purple Princess", 240000,
                  0, 0, 0, 0,  // origin on map 0
                  0, 0, 0, 0)  // destination also map 0 (same map)
    };
    CrossMapBerthResult r = ReadCrossMapBerth(MakeFacts(std::move(transports), {}, 0, 0, 0, 0, 10, 5));
    Check("same-map transport -> transportFound false", r.transportFound, false);
}

void TransportWithUnknownBerth()
{
    // Transport serves both maps but berth is unknown at origin.
    TransportBerthInfo t = Transport(181646, "Elune's Blessing", 300000,
                                     0, -4265, -11317, 0.57f,  // berth unknown at origin
                                     1, 0, 0, 0);
    t.stops[0].berthKnown = false;
    std::vector<TransportBerthInfo> transports = {t};
    CrossMapBerthResult r = ReadCrossMapBerth(MakeFacts(std::move(transports), {}, 0, 1, -8000, -500, 10, 5));
    Check("unknown berth -> transportFound false", r.transportFound, false);
}

void TransportWithKnownBerth()
{
    // Ratchet to Booty Bay: both berths known.
    CrossMapBerthResult r = ReadCrossMapBerth(BasicFacts());
    Check("known berth -> transportFound true", r.transportFound, true);
    Check("known berth -> berthKnown true", r.berthKnown, true);
    CheckFloat("berth X", r.berthX, -1005.f);
    CheckFloat("berth Y", r.berthY, -3845.f);
    CheckFloat("berth Z", r.berthZ, 6.f);
    CheckInt("transport entry", r.transportEntry, 176310);
    CheckString("transport name", r.transportName, "Ratchet Booty Bay Boat");
}

void PicksClosestTransport()
{
    // Two transports serve the same maps: one closer, one farther.
    std::vector<TransportBerthInfo> transports = {
        // Theramore to Menethil: ~4000 yards from leader
        Transport(176231, "Theramore Menethil Boat", 300000,
                  0, -3800, -500, 5.f,
                  1, -3800, -500, 5.f),
        // Ratchet to Booty Bay: ~6000 yards from leader
        Transport(176310, "Ratchet Booty Bay Boat", 420000,
                  0, -1005, -3845, 6.f,
                  1, -1005, -3845, 6.f)
    };
    CrossMapBerthResult r = ReadCrossMapBerth(MakeFacts(std::move(transports), {}, 0, 1, -8000, -500, 10, 5));
    Check("picks closer transport", r.transportFound, true);
    CheckInt("picks Theramore (closer)", r.transportEntry, 176231);
    CheckFloat("berth X", r.berthX, -3800.f);
}

void BerthGuardedByHighLevelHostile()
{
    // Transport with berth known, but guarded by level 20 mob (member is 10, gap 5).
    CrossMapBerthResult r = ReadCrossMapBerth(MakeFacts(
        {Transport(176310, "Ratchet Booty Bay Boat", 420000,
                  0, -1005, -3845, 6.f,
                  1, -1005, -3845, 6.f)},
        {Hostile(20)},  // level 20 > member 10 + gap 5
        0, 1, -8000, -500, 10, 5));
    Check("guarded berth -> berthGuarded true", r.berthGuarded, true);
    CheckInt("guard level is 20", r.berthGuardLevel, 20);
}

void BerthNotGuardedByLowLevelHostile()
{
    // Hostile at level 12: member is 10, gap 5 -> 10+5=15, so 12 is below threshold.
    CrossMapBerthResult r = ReadCrossMapBerth(MakeFacts(
        {Transport(176310, "Ratchet Booty Bay Boat", 420000,
                  0, -1005, -3845, 6.f,
                  1, -1005, -3845, 6.f)},
        {Hostile(12)},  // level 12 < 15 threshold
        0, 1, -8000, -500, 10, 5));
    Check("low-level hostile -> berthGuarded false", r.berthGuarded, false);
    CheckInt("guard level is 0", r.berthGuardLevel, 0);
}

void DestinationMapMissingBerth()
{
    // Transport has berth at origin but not at destination.
    TransportBerthInfo t = Transport(176310, "Ratchet Booty Bay Boat", 420000,
                                     0, -1005, -3845, 6.f,
                                     1, -1005, -3845, 6.f);
    t.stops[1].berthKnown = false;
    std::vector<TransportBerthInfo> transports = {t};
    CrossMapBerthResult r = ReadCrossMapBerth(MakeFacts(std::move(transports), {}, 0, 1, -8000, -500, 10, 5));
    Check("unknown landing -> transportFound false", r.transportFound, false);
}

void OriginMapNotInTransport()
{
    // Transport serves map 1 and map 530, not map 0.
    std::vector<TransportBerthInfo> transports = {
        Transport(181646, "Elune's Blessing", 300000,
                  530, -4265, -11317, 5.57f,  // Azuremyst
                  1, -8000, -500, 5.f)        // Kalimdor
    };
    CrossMapBerthResult r = ReadCrossMapBerth(MakeFacts(std::move(transports), {}, 530, 1, -8000, -500, 10, 5));
    Check("origin map not served -> transportFound false", r.transportFound, false);
}

void DestinationMapNotInTransport()
{
    // Transport serves map 0 and map 530, not map 1.
    std::vector<TransportBerthInfo> transports = {
        Transport(181646, "Elune's Blessing", 300000,
                  0, -4265, -11317, 5.57f,
                  530, -8000, -500, 5.f)
    };
    CrossMapBerthResult r = ReadCrossMapBerth(MakeFacts(std::move(transports), {}, 0, 530, -8000, -500, 10, 5));
    Check("destination map not served -> transportFound false", r.transportFound, false);
}

void ZeroPeriodTransport()
{
    // Transport with zero path time (invalid).
    TransportBerthInfo t = Transport(12345, "Broken Transport", 0,
                                     0, -1005, -3845, 6.f,
                                     1, -1005, -3845, 6.f);
    std::vector<TransportBerthInfo> transports = {t};
    CrossMapBerthResult r = ReadCrossMapBerth(MakeFacts(std::move(transports), {}, 0, 1, -8000, -500, 10, 5));
    Check("zero period -> transportFound false", r.transportFound, false);
}

}  // namespace

int main()
{
    NoTransportFound();
    TransportNotServingBothMaps();
    TransportWithUnknownBerth();
    TransportWithKnownBerth();
    PicksClosestTransport();
    BerthGuardedByHighLevelHostile();
    BerthNotGuardedByLowLevelHostile();
    DestinationMapMissingBerth();
    OriginMapNotInTransport();
    DestinationMapNotInTransport();
    ZeroPeriodTransport();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("cross-map berth reading: all checks passed\n");
    return 0;
}