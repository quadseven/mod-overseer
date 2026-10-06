/*
 * cross-to-map: one bot, any bot, sails to another continent by its faction's
 * own boat or zeppelin.
 *
 * The row grammar, one check per gate row (in the order the gate reads them),
 * which refusals a later ask can pass, the result JSON, and the route filter
 * the gate's `routeKnown` fact comes from: PriceCrossings over the module's own
 * crossing catalogue, one case per transport the dev realm's catalogue holds.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace D = OverseerDecisions;

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

bool Is(char const* got, char const* want)
{
    return std::strcmp(got, want) == 0;
}

void TheRowIsRoutedOnItsFirstWord()
{
    Check("routed", D::IsCrossRow("cross-to-map map:1"));
    Check("tabs and padding", D::IsCrossRow("  cross-to-map\tmap:0 "));
    Check("walk-to-spawn is not a cross row", !D::IsCrossRow("walk-to-spawn creature:5"));
    Check("party-walk is not a cross row", !D::IsCrossRow("party-walk creature:5"));
    Check("empty is no row", !D::IsCrossRow(""));
}

void TheRowNamesOneMap()
{
    D::CrossRequest r = D::ParseCrossRequest("cross-to-map map:1");
    Check("map 1 parses", !*r.error && r.map == 1);
    r = D::ParseCrossRequest("cross-to-map map:0");
    Check("map 0 (Eastern Kingdoms) parses", !*r.error && r.map == 0);
    r = D::ParseCrossRequest("cross-to-map   map:571");
    Check("spacing parses", !*r.error && r.map == 571);
    for (char const* bad : {"cross-to-map", "cross-to-map 1", "cross-to-map map:", "cross-to-map map:x",
                            "cross-to-map map:-1", "cross-to-map map:123456",
                            "cross-to-map map:1 map:0", "cross-to-map map:1 extra",
                            "cross-to-map MAP:1", "walk-to-spawn map:1", ""})
    {
        r = D::ParseCrossRequest(bad);
        Check(bad, Is(r.error, D::CrossRefusal::Malformed));
    }
}

D::CrossFacts Fine()
{
    return D::CrossFacts{};
}

void EachGateRowRefusesByName()
{
    namespace R = D::CrossRefusal;
    Check("a fine bot passes", Is(D::CrossGate(Fine()), ""));

    D::CrossFacts f = Fine();
    f.enabled = false;
    Check("master switch off", Is(D::CrossGate(f), R::Disabled));
    f = Fine();
    f.isBot = false;
    Check("not a bot", Is(D::CrossGate(f), R::NotABot));
    f = Fine();
    f.inWorld = false;
    Check("not in world", Is(D::CrossGate(f), R::NotInWorld));
    f = Fine();
    f.alive = false;
    Check("dead", Is(D::CrossGate(f), R::Dead));
    f = Fine();
    f.inCombat = true;
    Check("in combat", Is(D::CrossGate(f), R::InCombat));
    f = Fine();
    f.inInstance = true;
    Check("in an instance", Is(D::CrossGate(f), R::InInstance));
    f = Fine();
    f.inFlight = true;
    Check("in flight", Is(D::CrossGate(f), R::InFlight));
    f = Fine();
    f.onTargetMap = true;
    Check("already on that map", Is(D::CrossGate(f), R::AlreadyThere));
    f = Fine();
    f.alreadyCrossing = true;
    Check("already crossing", Is(D::CrossGate(f), R::AlreadyCrossing));
    f = Fine();
    f.busy = true;
    Check("on another verb's walk or hold", Is(D::CrossGate(f), R::Busy));
    f = Fine();
    f.routeKnown = false;
    Check("no route", Is(D::CrossGate(f), R::NoRoute));
    f = Fine();
    f.campaignArmed = true;
    Check("campaign armed or running", Is(D::CrossGate(f), R::Campaign));
    f = Fine();
    f.crossingsUnderWay = 2;
    Check("realm cap of two reached", Is(D::CrossGate(f), R::AtCap));
    f.crossingsUnderWay = 1;
    Check("one under way, two allowed", Is(D::CrossGate(f), ""));
    f = Fine();
    f.atOnce = 0;
    Check("AtOnce 0 refuses every crossing", Is(D::CrossGate(f), R::AtCap));
    Check("default cap is two", D::CROSS_AT_ONCE == 2);
}

void TheGateReadsItsRowsInOrder()
{
    namespace R = D::CrossRefusal;
    D::CrossFacts f;
    f.enabled = false;
    f.isBot = false;
    Check("switch before bot", Is(D::CrossGate(f), R::Disabled));
    f = Fine();
    f.isBot = false;
    f.inWorld = false;
    Check("bot before world", Is(D::CrossGate(f), R::NotABot));
    f = Fine();
    f.campaignArmed = true;
    f.crossingsUnderWay = 9;
    Check("campaign before cap", Is(D::CrossGate(f), R::Campaign));
    f = Fine();
    f.routeKnown = false;
    f.campaignArmed = true;
    Check("route before campaign", Is(D::CrossGate(f), R::NoRoute));
    f = Fine();
    f.busy = true;
    f.routeKnown = false;
    Check("busy before route", Is(D::CrossGate(f), R::Busy));
    f = Fine();
    f.onTargetMap = true;
    f.routeKnown = false;
    Check("already there before route", Is(D::CrossGate(f), R::AlreadyThere));
}

void ARefusalSaysWhetherAskingAgainCanHelp()
{
    namespace R = D::CrossRefusal;
    for (char const* soon : {R::NotInWorld, R::Busy, R::Dead, R::InCombat, R::InFlight, R::Campaign,
                             R::AtCap, R::GroundGaveUp, R::TimedOut, R::Died, R::Gone})
        Check(soon, D::CrossRefusalRetryable(soon));
    for (char const* never : {R::Malformed, R::Disabled, R::NotABot, R::InInstance,
                              R::AlreadyThere, R::AlreadyCrossing, R::NoRoute, R::LastYards})
        Check(never, !D::CrossRefusalRetryable(never));
    Check("unknown is not retryable", !D::CrossRefusalRetryable("something else"));
}

void TheResultCarriesFiveFields()
{
    std::string j = D::CrossResultJson(D::CrossEnd::Arrived, "", false, 1, 42);
    Check("arrived json", j == "{\"outcome\":\"arrived\",\"reason\":\"\",\"retry\":false,"
                               "\"map\":1,\"steps\":42}");
    j = D::CrossResultJson(D::CrossEnd::Refused, D::CrossRefusal::NoRoute, false, 0, 0);
    Check("refused json", j.find("\"outcome\":\"refused\"") != std::string::npos &&
                              j.find("\"retry\":false") != std::string::npos &&
                              j.find("\"map\":0") != std::string::npos);
    j = D::CrossResultJson(D::CrossEnd::TimedOut, D::CrossRefusal::TimedOut, true, 1, 360);
    Check("timeout json", j.find("\"outcome\":\"timeout\"") != std::string::npos &&
                              j.find("\"retry\":true") != std::string::npos &&
                              j.find("\"steps\":360") != std::string::npos);
    j = D::CrossResultJson(D::CrossEnd::Refused, "a \"quoted\" \\ reason\n", false, 1, 1);
    Check("reason is escaped", j.find("a \\\"quoted\\\" \\\\ reason ") != std::string::npos);
    Check("words", Is(D::CrossEndWord(D::CrossEnd::Arrived), "arrived") &&
                       Is(D::CrossEndWord(D::CrossEnd::Refused), "refused") &&
                       Is(D::CrossEndWord(D::CrossEnd::TimedOut), "timeout"));
}

// THE ROUTE TABLE. The realm's own crossing catalogue as the dev worldserver's
// log lists it, one case per transport that calls on two maps: whose crew it
// carries (the faction filter, from ReadCrewWelcome over the crew's reaction
// toward the walker), and whether each end has a berth level with the deck.
// The gate's `routeKnown` is "PriceCrossings picked one".
struct Boat
{
    char const* name;
    std::uint32_t entry;
    bool hordeCrew;      // friendly to the Horde, hostile to the Alliance
    bool berthAtOrigin;  // a surveyed berth at the stop on the walker's map
    bool landing;        // and at the far stop
};

D::CrossingOffer OfferFor(Boat const& b, bool walkerIsHorde)
{
    D::CrossingOffer o;
    o.entry = b.entry;
    bool const mine = b.hordeCrew == walkerIsHorde;
    o.crew = D::ReadCrewWelcome(10, mine ? 0 : 10);
    o.berthKnown = b.berthAtOrigin;
    o.landingKnown = b.landing;
    o.toBerthYards = 100.f;
    o.landingToGoalYards = 0.f;
    o.rideSeconds = 60.f;
    o.periodSeconds = 240.f;
    return o;
}

bool RouteKnown(Boat const& b, bool walkerIsHorde)
{
    D::CrossingPriceLimits limits;
    limits.yardsPerSecond = 7.f;
    return D::PriceCrossings({OfferFor(b, walkerIsHorde)}, limits).pick >= 0;
}

void EachTransportInTheCatalogueIsARouteForItsOwnFactionOnly()
{
    // Which end lacks a berth is not in the log, only how many ends have one;
    // pricing reads both ends alike. The Ratchet to Booty Bay boat is neutral
    // and is checked below, apart from the two sides.
    Boat const table[] = {
        {"Zeppelin (The Thundercaller)", 164871, true, true, true},
        {"Zeppelin (The Iron Eagle)", 175080, true, true, false},
        {"Ship (The Lady Mehley)", 176231, true, true, true},
        {"Ship (The Bravery)", 176310, false, true, false},
        {"Ship, Night Elf (Elune's Blessing)", 181646, false, true, false},
        {"Zeppelin, Horde (The Mighty Wind)", 186238, true, true, true},
    };
    for (Boat const& b : table)
    {
        bool const bothBerths = b.berthAtOrigin && b.landing;
        std::string const n = b.name;
        Check((n + ": its own side may take it only with both berths surveyed").c_str(),
              RouteKnown(b, b.hordeCrew) == bothBerths);
        Check((n + ": the other side's crew refuses the walker").c_str(),
              !RouteKnown(b, !b.hordeCrew));
    }
    // The neutral Ratchet to Booty Bay boat: a goblin crew has no quarrel with either side.
    // Built from its own values (both berths surveyed, goblin crew), not from a zeppelin's.
    D::CrossingOffer neutral;
    neutral.entry = 20808;
    neutral.crew = D::ReadCrewWelcome(8, 0);
    neutral.berthKnown = true;
    neutral.landingKnown = true;
    neutral.toBerthYards = 100.f;
    neutral.landingToGoalYards = 0.f;
    neutral.rideSeconds = 60.f;
    neutral.periodSeconds = 240.f;
    D::CrossingPriceLimits limits;
    limits.yardsPerSecond = 7.f;
    Check("Maiden's Fancy (neutral crew) is a route for either side",
          D::PriceCrossings({neutral}, limits).pick >= 0);
    Check("no offers is no route", D::PriceCrossings({}, limits).pick < 0);
}

}  // namespace

int main()
{
    TheRowIsRoutedOnItsFirstWord();
    TheRowNamesOneMap();
    EachGateRowRefusesByName();
    TheGateReadsItsRowsInOrder();
    ARefusalSaysWhetherAskingAgainCanHelp();
    TheResultCarriesFiveFields();
    EachTransportInTheCatalogueIsARouteForItsOwnFactionOnly();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
