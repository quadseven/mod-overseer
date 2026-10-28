/*
 * A long errand to a place reads its road before the family walks it (#836).
 *
 * MEASURED ON THE DEV REALM, 2026-10-04. A family of levels 37 to 41 died 23
 * times in 24 hours between runs, nearly all with job `town run`; 12 of those
 * deaths were in Burning Steppes (creatures 50 to 58). The newest rows were
 * aimed at a place by the Stormwind inn, from Gnomeregan, and the travel
 * survey's road between the two halves of the Eastern Kingdoms runs through
 * Searing Gorge and Burning Steppes. The hearthstone was usually on cooldown.
 *
 * A player of that level takes the tram or a flight, or waits for the
 * hearthstone. So the travel drive reads the planned road of a long walk to a
 * place at the youngest member's level, and a road that fails the far walk's
 * rule is not walked: the walker hearths when its bind is beside the aim and
 * the stone is ready, waits for the stone when it is not, and otherwise the
 * errand is refused.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * siblings, plus a read of the module source for the wiring.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace OverseerDecisions;

namespace
{

int failures = 0;

void CheckStep(char const* what, ErrandRoadStep got, ErrandRoadStep want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, ErrandRoadStepWord(got),
                ErrandRoadStepWord(want));
    ++failures;
}

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckText(char const* what, std::string const& got, char const* want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: '%s', wanted '%s'\n", what, got.c_str(), want);
    ++failures;
}

// A road read at the far walk's thirty yard spacing: `safe` samples of nothing
// above the walker, then `lethal` samples of the worst creature `worst`, then
// `safe` again. Burning Steppes at 37 is 50 to 58.
RouteVerdict Road(uint32_t level, std::size_t safe, std::size_t lethal, uint32_t worst)
{
    RouteReading reading;
    reading.characterLevel = level;
    reading.sampleSpacingYards = 30.f;
    for (std::size_t i = 0; i < safe; ++i)
        reading.worstLevelAtSample.push_back(0);
    for (std::size_t i = 0; i < lethal; ++i)
        reading.worstLevelAtSample.push_back(worst);
    for (std::size_t i = 0; i < safe; ++i)
        reading.worstLevelAtSample.push_back(0);
    return JudgeRoute(reading, FarWalkRoadLimits());
}

// The measured trip: a leader of 37 at the Gnomeregan exit, aimed at a place by
// the Stormwind inn about 3,700 yards off, bound at that inn.
ErrandRoadFacts GnomereganToStormwind()
{
    ErrandRoadFacts facts;
    facts.walkYards = 3700.f;
    facts.placeAim = true;
    facts.road = Road(37, 40, 30, 54);   // 900 yards of Burning Steppes
    facts.carriesStone = true;
    facts.hearthReady = false;
    facts.bindYardsFromAim = 22.f;
    return facts;
}

void TheSteppesRoadIsLethalAtTheFamilysLevel()
{
    Check("Burning Steppes at 37 fails the far walk's rule",
          GnomereganToStormwind().road.survivable, false);
    // And at 41, the oldest member: 50 is still nine over, but 54 is thirteen.
    Check("and at 41", Road(41, 40, 30, 54).survivable, false);
    // A road with nothing ten over on it is the road the family walks safely.
    Check("Elwynn at 37 is survivable", Road(37, 100, 0, 0).survivable, true);
}

void TheGnomereganExitWaitsForItsHearthstone()
{
    // The acceptance case: hearthstone on cooldown, bound at the aim's inn.
    // It does not cross Burning Steppes; it waits for the stone.
    CheckStep("stone on cooldown, bound at the aim", DecideErrandRoad(GnomereganToStormwind()),
              ErrandRoadStep::WaitForHearth);

    ErrandRoadFacts ready = GnomereganToStormwind();
    ready.hearthReady = true;
    CheckStep("stone ready, bound at the aim", DecideErrandRoad(ready), ErrandRoadStep::Hearth);
}

void NothingCarriesItButTheRoad()
{
    ErrandRoadFacts elsewhere = GnomereganToStormwind();
    elsewhere.bindYardsFromAim = 2400.f;   // bound in Ironforge
    elsewhere.hearthReady = true;
    CheckStep("bound far from the aim", DecideErrandRoad(elsewhere), ErrandRoadStep::Refuse);

    ErrandRoadFacts otherMap = GnomereganToStormwind();
    otherMap.bindYardsFromAim = -1.f;      // bound in Ratchet
    CheckStep("bound on another map", DecideErrandRoad(otherMap), ErrandRoadStep::Refuse);

    ErrandRoadFacts noStone = GnomereganToStormwind();
    noStone.carriesStone = false;
    CheckStep("no hearthstone", DecideErrandRoad(noStone), ErrandRoadStep::Refuse);
}

void ASafeRoadIsWalkedAsBefore()
{
    ErrandRoadFacts safe = GnomereganToStormwind();
    safe.road = Road(37, 120, 0, 0);
    CheckStep("a safe road", DecideErrandRoad(safe), ErrandRoadStep::Walk);

    // Unread is survivable: no survey, no route, no claim made about it.
    ErrandRoadFacts unread = GnomereganToStormwind();
    unread.road = RouteVerdict{};
    CheckStep("an unread road", DecideErrandRoad(unread), ErrandRoadStep::Walk);

    // A short lethal stretch is under the two hundred yards the rule allows.
    ErrandRoadFacts brush = GnomereganToStormwind();
    brush.road = Road(37, 60, 5, 54);
    CheckStep("150 yards of high ground", DecideErrandRoad(brush), ErrandRoadStep::Walk);
}

void OnlyALongWalkToAPlaceIsJudged()
{
    ErrandRoadFacts near = GnomereganToStormwind();
    near.walkYards = ERRAND_ROAD_MIN_YARDS - 1.f;
    CheckStep("under the journey line", DecideErrandRoad(near), ErrandRoadStep::Walk);

    ErrandRoadFacts creature = GnomereganToStormwind();
    creature.placeAim = false;
    CheckStep("an aim that is neither a place nor a spawn", DecideErrandRoad(creature),
              ErrandRoadStep::Walk);

    ErrandRoadFacts catchUp = GnomereganToStormwind();
    catchUp.catchUp = true;
    CheckStep("a catch-up walk (#697 judges it)", DecideErrandRoad(catchUp),
              ErrandRoadStep::Walk);

    ErrandRoadFacts run = GnomereganToStormwind();
    run.liveRun = true;
    CheckStep("a live run's walk", DecideErrandRoad(run), ErrandRoadStep::Walk);
}

// THE TWO WALKS THE FAMILY DIED ON AFTER #836 WENT LIVE (#1209). Measured
// 2026-10-18 to 2026-10-27: 55 deaths at levels 41 to 48 in Burning Steppes and
// Searing Gorge, nearly all of them members the road check never judged. The
// youngest member there was 42, and on 2026-10-25 the leader was sent back
// 13,044 yards for a member, the family behind him.
ErrandRoadFacts AtLevel(ErrandRoadFacts facts, uint32_t level)
{
    facts.road = Road(level, 40, 30, 54);   // the same 900 yards of Burning Steppes
    return facts;
}

void AFollowerIsHeldToItsLeadersRoad()
{
    // A follower walks where its leader walks, and the one leader walk that
    // was never judged is his going back for a member: it rides the catch-up
    // lease, and the catch-up exclusion let it through.
    ErrandRoadFacts fetch = AtLevel(GnomereganToStormwind(), 42);
    fetch.catchUp = true;
    fetch.leaderFetch = true;
    Check("the leader going back for a member is judged", ErrandRoadJudges(fetch), true);
    CheckStep("at 42, stone on cooldown, bound at the aim: not walked", DecideErrandRoad(fetch),
              ErrandRoadStep::WaitForHearth);

    ErrandRoadFacts unbound = fetch;
    unbound.bindYardsFromAim = 2400.f;
    CheckStep("at 42, bound elsewhere: refused", DecideErrandRoad(unbound),
              ErrandRoadStep::Refuse);

    ErrandRoadFacts grown = AtLevel(fetch, 55);
    Check("at 55 the steppes are survivable", grown.road.survivable, true);
    CheckStep("the same trip at 55 is walked", DecideErrandRoad(grown), ErrandRoadStep::Walk);

    // A MEMBER'S OWN CATCH-UP IS STILL #697's, judged by its lone leg rule.
    ErrandRoadFacts lone = fetch;
    lone.leaderFetch = false;
    Check("a member's own catch-up is not judged here", ErrandRoadJudges(lone), false);
    CheckStep("and is walked as before", DecideErrandRoad(lone), ErrandRoadStep::Walk);
}

void AVendorErrandIsHeldToTheSameRoad()
{
    // An aim at a spawn: its line was read when it was chosen, but at the
    // walker's level, and the family behind it is younger.
    ErrandRoadFacts vendor = AtLevel(GnomereganToStormwind(), 42);
    vendor.placeAim = false;
    vendor.spawnAim = true;
    Check("a long walk to a vendor is judged", ErrandRoadJudges(vendor), true);
    CheckStep("at 42, stone on cooldown, bound at the aim: not walked", DecideErrandRoad(vendor),
              ErrandRoadStep::WaitForHearth);

    ErrandRoadFacts ready = vendor;
    ready.hearthReady = true;
    CheckStep("at 42, stone ready: hearth", DecideErrandRoad(ready), ErrandRoadStep::Hearth);

    ErrandRoadFacts nowhere = vendor;
    nowhere.carriesStone = false;
    CheckStep("at 42, no stone: refused", DecideErrandRoad(nowhere), ErrandRoadStep::Refuse);

    CheckStep("the same trip at 55 is walked", DecideErrandRoad(AtLevel(vendor, 55)),
              ErrandRoadStep::Walk);

    ErrandRoadFacts near = vendor;
    near.walkYards = ERRAND_ROAD_MIN_YARDS - 1.f;
    Check("a vendor across the street is not a journey", ErrandRoadJudges(near), false);

    ErrandRoadFacts run = vendor;
    run.liveRun = true;
    CheckStep("a live run's vendor walk is the run's", DecideErrandRoad(run),
              ErrandRoadStep::Walk);
}

void TheWordsArePinned()
{
    ErrandRoadFacts fetch = GnomereganToStormwind();
    fetch.catchUp = true;
    fetch.leaderFetch = true;
    CheckText("fetch word", ErrandRoadWalkWord(fetch), "the leader going back for a member");
    ErrandRoadFacts spawn = GnomereganToStormwind();
    spawn.placeAim = false;
    spawn.spawnAim = true;
    CheckText("spawn word", ErrandRoadWalkWord(spawn), "a walk to a spawn");
    CheckText("place word", ErrandRoadWalkWord(GnomereganToStormwind()), "a walk to a place");

    CheckText("walk", ErrandRoadStepWord(ErrandRoadStep::Walk), "walk");
    CheckText("hearth", ErrandRoadStepWord(ErrandRoadStep::Hearth), "hearth");
    CheckText("wait", ErrandRoadStepWord(ErrandRoadStep::WaitForHearth), "wait for hearth");
    CheckText("refuse", ErrandRoadStepWord(ErrandRoadStep::Refuse), "refuse");
    CheckText("reason", ErrandLethalRoad,
              "the road to the errand crosses ground far above the family's level");
}

void TheTravelDriveAsksBeforeItWalks()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string const source = buffer.str();
    Check("the module source was read", !source.empty(), true);

    std::size_t const drive = source.find("    void DriveTravel()");
    std::size_t const asked = source.find("OverseerDecisions::DecideErrandRoad(", drive);
    std::size_t const walked =
        source.find("WorldPosition const leg = entry ? pos : RouteLeg(", drive);
    Check("DriveTravel exists", drive != std::string::npos, true);
    Check("DriveTravel asks the road decision", asked != std::string::npos, true);
    Check("and asks it before the walk is issued",
          asked != std::string::npos && walked != std::string::npos && asked < walked, true);
    std::size_t const flight = source.find("if (ConsiderFlight(name, bot, botAI, pos, distance, state))",
                                           drive);
    Check("and after the flight has had its turn",
          flight != std::string::npos && asked != std::string::npos && flight < asked, true);
    Check("the road is read under the far walk's rule",
          source.find("ReadErrandRoad(") != std::string::npos &&
              source.find("OverseerDecisions::FarWalkRoadLimits()") != std::string::npos,
          true);
    Check("a refused road is recorded against the aim",
          source.find("OverseerDecisions::ErrandLethalRoad") != std::string::npos, true);

    // #1209: the leader's fetch and a spawn aim reach the decision, and the
    // module asks the decision whether to read the road rather than its own copy.
    Check("the leader going back for a member is told to the decision",
          source.find("road.leaderFetch = road.catchUp && LeaderIsFetching(name);", drive) <
              asked,
          true);
    Check("a spawn aim is told to the decision",
          source.find("road.spawnAim = entry != 0;", drive) < asked, true);
    Check("the module asks the decision which walks it judges",
          source.find("if (OverseerDecisions::ErrandRoadJudges(road))", drive) < asked, true);
    Check("a spawn aim's line is read",
          source.find("road.spawnAim ? ReadErrandLine(bot, pos, state)", drive) < asked, true);
    std::size_t const refusedFetch =
        source.find("step == OverseerDecisions::ErrandRoadStep::Refuse && road.leaderFetch", drive);
    Check("a refused fetch ends the fetch",
          refusedFetch != std::string::npos &&
              source.find("EndFetch(name,", refusedFetch) < walked,
          true);
}

}  // namespace

int main()
{
    TheSteppesRoadIsLethalAtTheFamilysLevel();
    TheGnomereganExitWaitsForItsHearthstone();
    NothingCarriesItButTheRoad();
    ASafeRoadIsWalkedAsBefore();
    OnlyALongWalkToAPlaceIsJudged();
    AFollowerIsHeldToItsLeadersRoad();
    AVendorErrandIsHeldToTheSameRoad();
    TheWordsArePinned();
    TheTravelDriveAsksBeforeItWalks();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a long errand reads its road before the family walks it\n");
    return EXIT_SUCCESS;
}
