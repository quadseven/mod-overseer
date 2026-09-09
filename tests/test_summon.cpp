/*
 * Summoning an absent party member to a dungeon's summoning stone, and the
 * read-back that decides whether anybody crossed.
 *
 * mod-overseer#313. This family is split across two continents. Three members
 * stand on Kalimdor and two on Eastern Kingdoms, the dungeon they are trying to
 * run a hundred times is on Kalimdor, and the module says on every poll that
 * NOTHING IN IT CAN REJOIN THEM. The boat is filed six ways (#241, #274, #279,
 * #303, #304, #305) and has not landed. #308's hearthstone cannot help the two
 * that need it, because both are bound on the continent they are already
 * stranded on.
 *
 * The summoning stone is the game's own answer to exactly this, and the core
 * implements all of it. What the core does NOT do is press the accept button,
 * because a bot has no client to press it with, and the summoned character has
 * to accept or nothing moves.
 *
 * So this file pins the rules that decide, LATER, whether a summon crossed an
 * ocean, never left, or put somebody somewhere nobody asked for - plus the two
 * cases where no reading can decide and the honest answer is to say so.
 *
 * The other half of this verb's correctness is in tests/test_teleport_flight.cpp:
 * the accept ends in a FAR teleport, and a read-back that reads "still crossing"
 * as "gone" is #310, which this verb would otherwise have repeated.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::HomeBind;
using OverseerDecisions::ParseSummonRequest;
using OverseerDecisions::SummonOutcome;
using OverseerDecisions::SummonOutcomeWord;
using OverseerDecisions::SummonReadBack;
using OverseerDecisions::SummonRefusalRetry;
using OverseerDecisions::SummonRequest;
using OverseerDecisions::SummonVerb;
using OverseerDecisions::SummonVerifyWindowMs;
using OverseerDecisions::SummonWouldMoveNobody;
using OverseerDecisions::TownRetry;
using OverseerDecisions::TownRetryWord;

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

void CheckOutcome(char const* what, SummonOutcome got, SummonOutcome want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, SummonOutcomeWord(got),
                SummonOutcomeWord(want));
    ++failures;
}

void CheckWord(char const* what, char const* got, char const* want)
{
    if (std::string(got) == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got, want);
    ++failures;
}

void CheckText(char const* what, std::string const& got, char const* want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want);
    ++failures;
}

void CheckMs(char const* what, uint32_t got, uint32_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %u ms, wanted %u ms\n", what, unsigned(got), unsigned(want));
    ++failures;
}

// The tolerances mod_overseer.cpp passes in, named here so a failure reads as
// the rule rather than as a number.
constexpr float ARRIVED_YARDS = 60.f;
constexpr float MOVED_YARDS = 10.f;

HomeBind At(uint32_t map, float x, float y, float z)
{
    HomeBind p;
    p.known = true;
    p.mapId = map;
    p.areaId = 0;
    p.x = x;
    p.y = y;
    p.z = z;
    return p;
}

// Where this family actually stood, read off the live realm on 2026-09-08
// rather than invented, so the numbers a failure prints are numbers somebody
// can go and look at.
//
// Two members on map 0 and the stone on map 1. That gap is the whole reason
// this verb exists.
HomeBind StrandedInDunMorogh() { return At(0, -4915.89f, -999.79f, 501.997f); }
HomeBind StrandedInWetlands() { return At(0, -2955.7f, -2051.15f, 5.90319f); }

// gameobject.guid 15686, entry 178884, the one Wailing Caverns meeting stone
// spawn in the world database. The summon point is not this exactly - the spell
// effect reads the SUMMONER'S coordinates - but the summoner is standing within
// interaction distance of it, so this is the right order of magnitude and the
// right map.
HomeBind AtTheWailingCavernsStone() { return At(1, -793.299f, -2135.61f, 92.3452f); }

// A pace off the stone, which is what a bot that landed and took a few steps
// reads back as.
HomeBind AFewStepsOffTheStone() { return At(1, -790.1f, -2131.4f, 92.4f); }

// Same coordinates as the stone, wrong continent. THE READING THAT WOULD SINK
// THIS VERB if the map were not compared: the two continents share a coordinate
// space, so a distance check alone calls this an arrival.
HomeBind TheStoneCoordinatesOnTheWrongMap()
{
    return At(0, -793.299f, -2135.61f, 92.3452f);
}

HomeBind Unread() { return HomeBind(); }

// ---------------------------------------------------------------- parsing --

void ANameIsRequiredAndTwoFormsGiveIt()
{
    SummonRequest bare = ParseSummonRequest("Grug");
    Check("a bare name parses", bare.verb == SummonVerb::Use, true);
    CheckText("a bare name is the name", bare.who, "Grug");
    CheckText("a bare name refuses nothing", bare.error, "");

    SummonRequest spelled = ParseSummonRequest("use Ugga");
    Check("use plus a name parses", spelled.verb == SummonVerb::Use, true);
    CheckText("use plus a name is the name", spelled.who, "Ugga");

    SummonRequest padded = ParseSummonRequest("   use    Grug   ");
    Check("whitespace does not change the request", padded.verb == SummonVerb::Use, true);
    CheckText("whitespace does not change the name", padded.who, "Grug");
}

// THE ONE PLACE THIS PARSER DELIBERATELY DIFFERS FROM ParseHearthRequest. That
// verb reads an empty command as `use`, because it has exactly one form and no
// arguments. This one has an argument and four party members it could mean, so
// an empty column is a refusal with words rather than a guess.
void AnEmptyRequestIsRefusedAndNotGuessedAt()
{
    SummonRequest empty = ParseSummonRequest("");
    Check("empty does not parse", empty.verb == SummonVerb::None, true);
    CheckText("empty says what is missing", empty.error,
              "malformed summon: name the character to summon");
    CheckText("a refused request names nobody", empty.who, "");

    SummonRequest blank = ParseSummonRequest("    ");
    Check("whitespace only does not parse", blank.verb == SummonVerb::None, true);
    CheckText("whitespace only says the same thing", blank.error,
              "malformed summon: name the character to summon");

    SummonRequest lonely = ParseSummonRequest("use");
    Check("use with no name does not parse", lonely.verb == SummonVerb::None, true);
    CheckText("use with no name says so", lonely.error,
              "malformed summon: use wants the character to summon");
}

void ARequestMayNameExactlyOneCharacter()
{
    SummonRequest two = ParseSummonRequest("use Grug Ugga");
    Check("use with two names does not parse", two.verb == SummonVerb::None, true);
    CheckText("use with two names says so", two.error,
              "malformed summon: use takes one name and no more");

    SummonRequest bareTwo = ParseSummonRequest("Grug Ugga");
    Check("two bare names do not parse", bareTwo.verb == SummonVerb::None, true);
    CheckText("two bare names say so", bareTwo.error,
              "malformed summon: want one name, or use and one name");
}

// -------------------------------------------------------------- read-back --

void CrossingIsTheOnlySuccess()
{
    CheckOutcome("landed on the stone",
                 SummonReadBack(StrandedInDunMorogh(), AtTheWailingCavernsStone(),
                                AtTheWailingCavernsStone(), ARRIVED_YARDS, MOVED_YARDS),
                 SummonOutcome::Arrived);

    // A bot lands and then walks, because it has a drive of its own and the
    // verdict is taken a margin later. Measuring arrival to the yard would
    // report that as `elsewhere`.
    CheckOutcome("landed and took a few steps",
                 SummonReadBack(StrandedInWetlands(), AtTheWailingCavernsStone(),
                                AFewStepsOffTheStone(), ARRIVED_YARDS, MOVED_YARDS),
                 SummonOutcome::Arrived);
}

// THE FAILURE THIS VERB EXISTS TO MAKE VISIBLE. Five different things produce
// it and they are identical from the queue: no second clicker, a selection lost
// before the ritual settled, no request sent, an accept refused for combat or
// death, or a far teleport nobody acknowledged. Every one of them is a
// character standing exactly where it started, and not one of them is applied.
void NeverHavingLeftIsTheFailureThisExistsToSee()
{
    CheckOutcome("still in Dun Morogh",
                 SummonReadBack(StrandedInDunMorogh(), AtTheWailingCavernsStone(),
                                StrandedInDunMorogh(), ARRIVED_YARDS, MOVED_YARDS),
                 SummonOutcome::Stayed);

    // A pace of drift is still not having gone anywhere. The narrow tolerance
    // is what absorbs it.
    CheckOutcome("drifted four yards and crossed no ocean",
                 SummonReadBack(StrandedInWetlands(), AtTheWailingCavernsStone(),
                                At(0, -2952.7f, -2049.15f, 5.90319f),
                                ARRIVED_YARDS, MOVED_YARDS),
                 SummonOutcome::Stayed);
}

// THE READING THAT WOULD SINK THIS VERB. Kalimdor and Eastern Kingdoms share a
// coordinate space. A comparison that forgot the map would call a character
// standing on the wrong continent, at the stone's exact coordinates, an
// arrival - and this family is split across exactly those two continents.
void TheSameCoordinatesOnTheWrongContinentAreNotAnArrival()
{
    CheckOutcome("stone coordinates, wrong map",
                 SummonReadBack(StrandedInDunMorogh(), AtTheWailingCavernsStone(),
                                TheStoneCoordinatesOnTheWrongMap(),
                                ARRIVED_YARDS, MOVED_YARDS),
                 SummonOutcome::Elsewhere);
}

void MovingSomewhereNobodyAskedForIsItsOwnAnswer()
{
    // Died mid-ritual and released to a graveyard on its own continent. The
    // summon did not work and the character is not where it was, and those are
    // two different facts.
    CheckOutcome("released to a graveyard instead",
                 SummonReadBack(StrandedInDunMorogh(), AtTheWailingCavernsStone(),
                                At(0, -5605.f, -750.f, 400.f),
                                ARRIVED_YARDS, MOVED_YARDS),
                 SummonOutcome::Elsewhere);

    // Arrived on the right map and then kept walking well past the tolerance.
    // Still not an arrival, and saying `stayed` about it would be a lie in the
    // other direction.
    CheckOutcome("on the right map and nowhere near the stone",
                 SummonReadBack(StrandedInWetlands(), AtTheWailingCavernsStone(),
                                At(1, -989.833f, -3546.04f, 22.8623f),
                                ARRIVED_YARDS, MOVED_YARDS),
                 SummonOutcome::Elsewhere);
}

void AReadingNobodyTookIsNoneOfTheThree()
{
    CheckOutcome("no start line",
                 SummonReadBack(Unread(), AtTheWailingCavernsStone(),
                                AtTheWailingCavernsStone(), ARRIVED_YARDS, MOVED_YARDS),
                 SummonOutcome::Unreadable);
    CheckOutcome("no summon point",
                 SummonReadBack(StrandedInDunMorogh(), Unread(),
                                AtTheWailingCavernsStone(), ARRIVED_YARDS, MOVED_YARDS),
                 SummonOutcome::Unreadable);
    CheckOutcome("no finish line",
                 SummonReadBack(StrandedInDunMorogh(), AtTheWailingCavernsStone(), Unread(),
                                ARRIVED_YARDS, MOVED_YARDS),
                 SummonOutcome::Unreadable);
}

// When the character was already at the summon point, `Arrived` and `Stayed`
// are the same reading. The executor refuses that row before the packet, so
// reaching here means the summoner moved while the ritual was settling - which
// it may, because the summon point is the summoner's own position and a bot
// walks. Saying so is honest; picking one of the two would be a coin toss
// reported as a measurement.
void AStartLineThatIsAlsoTheFinishCannotBeJudged()
{
    CheckOutcome("started at the summon point",
                 SummonReadBack(AtTheWailingCavernsStone(), AtTheWailingCavernsStone(),
                                AtTheWailingCavernsStone(), ARRIVED_YARDS, MOVED_YARDS),
                 SummonOutcome::Unreadable);
    CheckOutcome("started inside the arrival tolerance of it",
                 SummonReadBack(AFewStepsOffTheStone(), AtTheWailingCavernsStone(),
                                At(1, -700.f, -2100.f, 92.f), ARRIVED_YARDS, MOVED_YARDS),
                 SummonOutcome::Unreadable);
}

// The same question asked BEFORE the ritual, where the answer is a refusal
// rather than a verdict. A summon that would move nobody costs two characters a
// ritual to find out something no reading could have told anybody afterwards.
void ASummonThatWouldMoveNobodyIsRefusedInAdvance()
{
    Check("already standing at the summon point",
          SummonWouldMoveNobody(AtTheWailingCavernsStone(), AtTheWailingCavernsStone(),
                                ARRIVED_YARDS),
          true);
    Check("a few steps away is still already there",
          SummonWouldMoveNobody(AFewStepsOffTheStone(), AtTheWailingCavernsStone(),
                                ARRIVED_YARDS),
          true);
    Check("on the other continent is not already there",
          SummonWouldMoveNobody(StrandedInDunMorogh(), AtTheWailingCavernsStone(),
                                ARRIVED_YARDS),
          false);
    Check("the same coordinates on the wrong map are not already there",
          SummonWouldMoveNobody(TheStoneCoordinatesOnTheWrongMap(),
                                AtTheWailingCavernsStone(), ARRIVED_YARDS),
          false);

    // A reading nobody took is not a reason to refuse. The executor asks about
    // the readings themselves first, and answering `true` here would refuse
    // every character whose position could not be read, which is a different
    // refusal wearing this one's words.
    Check("an unread character is not refused by this",
          SummonWouldMoveNobody(Unread(), AtTheWailingCavernsStone(), ARRIVED_YARDS),
          false);
    Check("an unread summon point is not refused by this",
          SummonWouldMoveNobody(StrandedInDunMorogh(), Unread(), ARRIVED_YARDS),
          false);
}

// ---------------------------------------------------------------- windows --

void TheWaitComesFromTheRitualAndNotFromAConstant()
{
    // The ritual settles five seconds after the last participant clicks, which
    // is the core's own number and not this module's.
    CheckMs("five second settle plus a five second margin",
            SummonVerifyWindowMs(5000, 5000, 8000), 10000);

    // A settle time that reads as zero would judge instantly and therefore
    // always answer `stayed`. The floor is what stops that.
    CheckMs("a zero settle is floored", SummonVerifyWindowMs(0, 0, 8000), 8000);
    CheckMs("a short settle is floored", SummonVerifyWindowMs(1000, 1000, 8000), 8000);

    // Saturating, not wrapping. An absurd settle time must not become a window
    // shorter than the floor by overflowing - that is precisely the reading
    // that judges a summon while the ritual is still running.
    CheckMs("a settle at the top of the range saturates",
            SummonVerifyWindowMs(UINT32_MAX, 5000, 8000), UINT32_MAX);
    CheckMs("one below the top saturates too",
            SummonVerifyWindowMs(UINT32_MAX - 1, 5000, 8000), UINT32_MAX);
    CheckMs("an addition that just fits is not saturated",
            SummonVerifyWindowMs(UINT32_MAX - 5000, 5000, 8000), UINT32_MAX);
}

// --------------------------------------------------------------- refusals --

void EveryRefusalCarriesWhereToTryAgain()
{
    auto never = [](char const* detail)
    {
        CheckWord(detail, TownRetryWord(SummonRefusalRetry(detail)), "never");
    };
    auto elsewhere = [](char const* detail)
    {
        CheckWord(detail, TownRetryWord(SummonRefusalRetry(detail)), "elsewhere");
    };
    auto later = [](char const* detail)
    {
        CheckWord(detail, TownRetryWord(SummonRefusalRetry(detail)), "later");
    };

    // The request itself is the wall, and no state anywhere makes the same row
    // valid. Retyped rather than referenced, so a literal that drifts in
    // mod_overseer.cpp or in the parser fails here.
    never("malformed summon: name the character to summon");
    never("malformed summon: use wants the character to summon");
    never("malformed summon: use takes one name and no more");
    never("malformed summon: want one name, or use and one name");
    never("malformed summon request");
    never("a character cannot summon itself");
    never("the second clicker cannot be the summoner");
    never("the core does not know that meeting stone");
    never("the core does not know that summoning portal");

    // The stone is a place, and standing somewhere else is the whole answer.
    elsewhere("no meeting stone within reach of the summoner");
    elsewhere("no meeting stone within reach of the second clicker");
    elsewhere("no second party member is at the stone");
    elsewhere("the character to summon is already at the summon point");
    elsewhere("the character to summon cannot enter the instance the summoner is in");

    // Somebody's own state, and every one of these ends on its own.
    later("summoner has no session");
    later("summoner is not in the world");
    later("summoner is dead");
    later("summoner is in combat");
    later("summoner is in flight");
    later("summoner is moving");
    later("summoner is already casting");
    later("summoner is on a transport");
    later("summoner is not its own mover");
    later("summoner is below the minimum level of the stone");
    later("the character to summon is not online");
    later("the second clicker is not online");
    later("the character to summon is not in the same party");
    later("the character to summon is below the minimum level of the stone");
    later("the character to summon has no bot AI to acknowledge the teleport");
    later("the character to summon is dead");
    later("the character to summon is in combat");
    later("the character to summon is in flight");
    later("the character to summon is not in the world");
    later("the character to summon is already being teleported");
    later("the character to summon already has a summon pending");
    later("the second clicker is not in the same party");
    later("the second clicker is dead");
    later("the second clicker is in combat");
    later("the second clicker is not in the world");
    // THE TWO THAT A REVIEW ADDED. The second clicker holds a channel for the
    // whole settle window exactly as the summoner does, and nothing said so
    // until somebody asked: its click starts the portal's anim spell, which is
    // channelled with movement among its interrupt flags, so a helper that
    // walks is erased by CheckRitualList when the ritual settles and the summon
    // dies silently five seconds after everything looked fine.
    later("the second clicker is moving");
    later("the second clicker is already casting");
    later("the second clicker has no session");
    later("the summoner did not begin channelling the portal");
    later("the ritual did not reach the participants it needs");
    // AND THE TWO THE PORTAL'S OWN WAIT ADDS (#365). The literal above used to
    // be returned for a summoner that WAS channelling, because the verb looked
    // for the ritual object in the statement after the click and the core does
    // not create it until the next world tick. It now means only what it says,
    // and these two say what actually happens: the channel ended before a portal
    // appeared, or it is still running and none has. Both are about a moment
    // rather than a place, so the same pair on the same stone is the right thing
    // to try next. tests/test_summon_portal.cpp pins the decision behind them.
    later("the summoner stopped channelling before a portal appeared");
    later("the summoner is channelling but no portal appeared");

    // AND AN UNRECOGNISED LITERAL IS `later`, which is the same call the bind,
    // hearth, sell and repair tables make: a refusal this table has never heard
    // of is more likely a new transient than a new permanent. Pinned with a
    // string the executor never returns, so the fall-through is proven rather
    // than assumed.
    later("a refusal nothing in this module has ever written");
}

void TheOutcomeWordsAreTheOnesARowCarries()
{
    CheckWord("arrived", SummonOutcomeWord(SummonOutcome::Arrived), "arrived");
    CheckWord("stayed", SummonOutcomeWord(SummonOutcome::Stayed), "stayed");
    CheckWord("elsewhere", SummonOutcomeWord(SummonOutcome::Elsewhere), "elsewhere");
    CheckWord("unreadable", SummonOutcomeWord(SummonOutcome::Unreadable), "unreadable");
}

}  // namespace

int main()
{
    ANameIsRequiredAndTwoFormsGiveIt();
    AnEmptyRequestIsRefusedAndNotGuessedAt();
    ARequestMayNameExactlyOneCharacter();
    CrossingIsTheOnlySuccess();
    NeverHavingLeftIsTheFailureThisExistsToSee();
    TheSameCoordinatesOnTheWrongContinentAreNotAnArrival();
    MovingSomewhereNobodyAskedForIsItsOwnAnswer();
    AReadingNobodyTookIsNoneOfTheThree();
    AStartLineThatIsAlsoTheFinishCannotBeJudged();
    ASummonThatWouldMoveNobodyIsRefusedInAdvance();
    TheWaitComesFromTheRitualAndNotFromAConstant();
    EveryRefusalCarriesWhereToTryAgain();
    TheOutcomeWordsAreTheOnesARowCarries();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a summon is judged by where the summoned character reads back, and "
                "the wrong continent is never an arrival\n");
    return EXIT_SUCCESS;
}
