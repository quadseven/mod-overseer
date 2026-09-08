/*
 * Going home on the game's own hearthstone, and the read-back that decides
 * whether anybody went.
 *
 * mod-overseer#308. #286 gave this module a way to CHANGE a home, at an
 * innkeeper, through the core's own binder handler. Nothing could then go to
 * one. The four revival exits teleport a character to m_homebind* and every one
 * of them is a revival, argued for on its own; a fifth call to TeleportTo with
 * nothing in front of it would be an admin teleport wearing the name of a game
 * mechanic, which is the thing AGENTS.md tells this module never to reach for.
 *
 * So the verb is the item, and the item takes ten seconds. That is what this
 * file is really about. A cast that outlives the poll that started it cannot be
 * answered by the call that sent it, so the rules pinned here are the ones that
 * decide, LATER, whether a character went home, stayed exactly where it was, or
 * ended up somewhere nobody asked for - and the two cases where no reading can
 * decide at all and the honest answer is to say so.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using OverseerDecisions::HearthOutcome;
using OverseerDecisions::HearthOutcomeWord;
using OverseerDecisions::HearthReadBack;
using OverseerDecisions::HearthRefusalRetry;
using OverseerDecisions::HearthRequest;
using OverseerDecisions::HearthVerb;
using OverseerDecisions::HearthVerifyWindowMs;
using OverseerDecisions::HearthWouldMoveNobody;
using OverseerDecisions::HomeBind;
using OverseerDecisions::ParseHearthRequest;
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

void CheckOutcome(char const* what, HearthOutcome got, HearthOutcome want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, HearthOutcomeWord(got),
                HearthOutcomeWord(want));
    ++failures;
}

void CheckWord(char const* what, char const* got, char const* want)
{
    if (std::string(got) == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got, want);
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
constexpr float ARRIVED_YARDS = 40.f;
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

// The three places this family actually stands, read off the live realm on
// 2026-09-07 rather than invented, so the numbers a failure prints are numbers
// somebody can go and look at.
//
// One member is standing on Eastern Kingdoms (map 0) and is bound in Ratchet on
// Kalimdor (map 1). That single row is the whole reason this verb exists: it is
// the only member of the five for whom a hearthstone is a crossing.
HomeBind StandingInEasternKingdoms() { return At(0, -5175.f, -827.f, 496.f); }
HomeBind BoundInRatchetOnKalimdor() { return At(1, -1050.f, -3665.f, 6.f); }
HomeBind BoundInElwynnOnEasternKingdoms() { return At(0, -8950.f, -132.f, 84.f); }

void TheOnlyFormIsUseAndNothingAtAllMeansIt()
{
    HearthRequest const empty = ParseHearthRequest("");
    Check("empty is a use", empty.verb == HearthVerb::Use, true);
    Check("empty carries no error", empty.error.empty(), true);

    HearthRequest const spaces = ParseHearthRequest("   ");
    Check("whitespace is a use", spaces.verb == HearthVerb::Use, true);

    HearthRequest const word = ParseHearthRequest("use");
    Check("use is a use", word.verb == HearthVerb::Use, true);

    HearthRequest const padded = ParseHearthRequest("  use  ");
    Check("padded use is a use", padded.verb == HearthVerb::Use, true);

    // NO DESTINATION, EVER. A grammar that could name a map or a coordinate
    // would be a grammar for the teleport this verb exists in order not to be,
    // so the parser refuses one rather than ignoring it.
    HearthRequest const aimed = ParseHearthRequest("use 1 -1050 -3665");
    Check("a hearth cannot be aimed", aimed.verb == HearthVerb::None, true);
    CheckWord("aiming is refused by name", aimed.error.c_str(),
              "malformed hearth: use takes no arguments");

    HearthRequest const other = ParseHearthRequest("home");
    Check("another verb is not a hearth", other.verb == HearthVerb::None, true);
    CheckWord("an unknown verb says what it wanted", other.error.c_str(),
              "malformed hearth: unknown verb (want use, or nothing at all)");

    // Upstream's own word for the OTHER half of this, the one #286 replaced.
    // It sets a home; it never travelled to one. A row that asks for it here is
    // asking for a verb this executor does not have, and being told so is
    // better than being given the one that sounds similar.
    HearthRequest const setter = ParseHearthRequest("set");
    Check("set is not a hearth", setter.verb == HearthVerb::None, true);
}

void ArrivingIsTheOnlySuccess()
{
    HomeBind const from = StandingInEasternKingdoms();
    HomeBind const home = BoundInRatchetOnKalimdor();

    // The row the family would run tonight: one member leaves Eastern Kingdoms
    // and reads back in Ratchet, on the continent its dungeon is on.
    CheckOutcome("landed on the bind point", HearthReadBack(from, home, home, ARRIVED_YARDS,
                                                            MOVED_YARDS),
                 HearthOutcome::Arrived);

    // AND THIRTY YARDS AWAY ON THE RIGHT MAP IS STILL AN ARRIVAL, which is not
    // about the spell being imprecise. Spell::EffectTeleportUnits lands the
    // character on m_homebindX/Y/Z exactly. It is about what happens next: the
    // judgement is taken a margin after the cast ends, and the thing being
    // judged is a bot with a drive of its own. A character that landed at its
    // inn and walked a few steps has arrived.
    HomeBind const insideTheInn = At(1, -1050.f + 30.f, -3665.f, 6.f);
    CheckOutcome("thirty yards from the bind point is still home",
                 HearthReadBack(from, home, insideTheInn, ARRIVED_YARDS, MOVED_YARDS),
                 HearthOutcome::Arrived);
}

void NeverHavingLeftIsTheFailureThisExistsToSee()
{
    HomeBind const from = StandingInEasternKingdoms();
    HomeBind const home = BoundInRatchetOnKalimdor();

    // THE ROW AGENTS.md WARNS ABOUT. The packet went out, the queue would
    // happily have said `delivered`, and the character is standing exactly
    // where it was. An interrupt, a silent refusal, or a cast that never
    // started: from here they are one reading, and not one of them is success.
    CheckOutcome("still on the start line", HearthReadBack(from, home, from, ARRIVED_YARDS,
                                                           MOVED_YARDS),
                 HearthOutcome::Stayed);

    // A pace or two counts as not having moved. The tolerance is tighter than
    // the arrival's on purpose, because it answers a different question:
    // absorbing a few seconds of a bot's own walking is one job, and deciding
    // whether it went anywhere at all is another.
    HomeBind const shuffled = At(0, -5175.f + 4.f, -827.f + 3.f, 496.f);
    CheckOutcome("a pace is not a journey",
                 HearthReadBack(from, home, shuffled, ARRIVED_YARDS, MOVED_YARDS),
                 HearthOutcome::Stayed);
}

void MovingSomewhereNobodyAskedForIsItsOwnAnswer()
{
    HomeBind const from = StandingInEasternKingdoms();
    HomeBind const home = BoundInRatchetOnKalimdor();

    // Died mid-cast and released to the nearest graveyard: on the map it
    // started on, nowhere near either end. "It did not work" and "it went
    // somewhere else" want different answers from the sender, so they are
    // different words.
    HomeBind const graveyard = At(0, -4900.f, -1000.f, 300.f);
    CheckOutcome("a corpse run is not a hearth",
                 HearthReadBack(from, home, graveyard, ARRIVED_YARDS, MOVED_YARDS),
                 HearthOutcome::Elsewhere);

    // THE RIGHT COORDINATES ON THE WRONG MAP ARE NOT AN ARRIVAL. Two continents
    // share a coordinate space and this family is split across exactly those
    // two, so a comparison that forgot the map would report the crossing as
    // done while the character stood on the wrong side of the ocean. Same
    // numbers as the home, map 0 instead of map 1.
    HomeBind const rightSpotWrongContinent = At(0, -1050.f, -3665.f, 6.f);
    CheckOutcome("the map is part of the place",
                 HearthReadBack(from, home, rightSpotWrongContinent, ARRIVED_YARDS,
                                MOVED_YARDS),
                 HearthOutcome::Elsewhere);
}

void AReadingNobodyTookIsNoneOfTheThree()
{
    HomeBind const from = StandingInEasternKingdoms();
    HomeBind const home = BoundInRatchetOnKalimdor();
    HomeBind const unread;

    CheckOutcome("no start line", HearthReadBack(unread, home, home, ARRIVED_YARDS,
                                                 MOVED_YARDS),
                 HearthOutcome::Unreadable);
    CheckOutcome("no home", HearthReadBack(from, unread, home, ARRIVED_YARDS, MOVED_YARDS),
                 HearthOutcome::Unreadable);
    CheckOutcome("nowhere now", HearthReadBack(from, home, unread, ARRIVED_YARDS,
                                               MOVED_YARDS),
                 HearthOutcome::Unreadable);

    // An unread home is map 0 at the origin, and map 0 at the origin is a real
    // place in Eastern Kingdoms. `known` is what separates them, exactly as it
    // does for a bind.
    HomeBind const realOrigin = At(0, 0.f, 0.f, 0.f);
    Check("an unread home is not the origin", unread.known == realOrigin.known, false);
}

void AStartLineThatIsAlsoTheFinishCannotBeJudged()
{
    // The character is standing at its own home. `Arrived` and `Stayed` are the
    // same reading, so no post-condition can separate them and the honest
    // answer is that this one cannot be judged at all.
    HomeBind const home = BoundInElwynnOnEasternKingdoms();
    CheckOutcome("home is the start line", HearthReadBack(home, home, home, ARRIVED_YARDS,
                                                          MOVED_YARDS),
                 HearthOutcome::Unreadable);

    // Which is why the executor refuses it BEFORE the packet. An hour of
    // cooldown is too much to spend on a verdict that was never available.
    Check("standing at home moves nobody",
          HearthWouldMoveNobody(home, home, ARRIVED_YARDS), true);
    HomeBind const acrossTheRoom = At(0, -8950.f + 6.f, -132.f + 6.f, 84.f);
    Check("across the room is still home",
          HearthWouldMoveNobody(acrossTheRoom, home, ARRIVED_YARDS), true);

    // AND THE ROW THIS FAMILY WOULD ACTUALLY RUN IS NOT REFUSED. Standing on
    // Eastern Kingdoms, bound in Ratchet: a real crossing, and the one member
    // for whom this verb does anything at all tonight.
    Check("a continent apart is not nobody",
          HearthWouldMoveNobody(StandingInEasternKingdoms(), BoundInRatchetOnKalimdor(),
                                ARRIVED_YARDS),
          false);

    // A reading nobody took is not a reason to refuse under THIS name. The
    // executor has its own refusal for an unread home, and answering true here
    // would serve that refusal wearing this one's words.
    HomeBind const unread;
    Check("an unread home refuses under its own name",
          HearthWouldMoveNobody(unread, home, ARRIVED_YARDS), false);
    Check("an unread standing place likewise",
          HearthWouldMoveNobody(home, unread, ARRIVED_YARDS), false);
}

void TheWaitComesFromTheSpellAndNotFromAConstant()
{
    // VERIFY_GRACE_MS, the window every other read-back in this module uses.
    // Named here because the whole point of this function is that it is too
    // short for this verb: judging a ten second cast after six seconds answers
    // `Stayed` about a character that is standing there casting.
    constexpr uint32_t VERIFY_GRACE_MS = 6000;
    constexpr uint32_t MARGIN_MS = 5000;
    constexpr uint32_t FLOOR_MS = 8000;

    uint32_t const tenSeconds = HearthVerifyWindowMs(10000, MARGIN_MS, FLOOR_MS);
    CheckMs("a ten second cast waits fifteen", tenSeconds, 15000);
    Check("and that is longer than the strategy window", tenSeconds > VERIFY_GRACE_MS, true);

    // A cast time of zero is what a haste effect, a missing DBC rank and a core
    // that resolved nothing all look like from here. A zero window judges
    // instantly, and judging instantly always answers `Stayed`.
    CheckMs("a zero cast still waits the floor", HearthVerifyWindowMs(0, 0, FLOOR_MS),
            FLOOR_MS);
    CheckMs("a short cast is floored", HearthVerifyWindowMs(500, 500, FLOOR_MS), FLOOR_MS);

    // A long cast is not floored back down.
    CheckMs("a long cast keeps its own length",
            HearthVerifyWindowMs(30000, MARGIN_MS, FLOOR_MS), 35000);

    // SATURATING, NOT WRAPPING. A nonsense number out of the DBC must not
    // become a window shorter than the floor by overflowing, because that is
    // exactly the reading that reports a cast in progress as a failure.
    CheckMs("an absurd cast time saturates",
            HearthVerifyWindowMs(UINT32_MAX, MARGIN_MS, FLOOR_MS), UINT32_MAX);
    Check("and never comes back under the floor",
          HearthVerifyWindowMs(UINT32_MAX - 1, 10000, FLOOR_MS) >= FLOOR_MS, true);
}

void EveryRefusalCarriesWhereToTryAgain()
{
    // NEVER: the command or the item is the wall, and this row will hit it
    // again for as long as it exists.
    CheckWord("a malformed verb never succeeds",
              TownRetryWord(HearthRefusalRetry(
                  "malformed hearth: unknown verb (want use, or nothing at all)")),
              "never");
    CheckWord("an aimed hearth never succeeds",
              TownRetryWord(HearthRefusalRetry("malformed hearth: use takes no arguments")),
              "never");
    CheckWord("no hearthstone is the item, not the moment",
              TownRetryWord(HearthRefusalRetry("character carries no hearthstone")),
              "never");
    CheckWord("an item with no on-use spell will not grow one",
              TownRetryWord(HearthRefusalRetry("the hearthstone has no on-use spell")),
              "never");
    // The server's own data being wrong is never fixed by the character
    // waiting, and a sender told `later` about one of these would retry it for
    // ever.
    CheckWord("an item with no template at all",
              TownRetryWord(HearthRefusalRetry("the hearthstone has no template")), "never");
    CheckWord("a spell the core's DBC does not carry",
              TownRetryWord(HearthRefusalRetry("the core does not know that spell")), "never");

    // ELSEWHERE: this spot is the wall. Walking out answers all three.
    CheckWord("an arena is answered by leaving it",
              TownRetryWord(HearthRefusalRetry("character is in an arena")), "elsewhere");
    CheckWord("a deck is answered by stepping off it",
              TownRetryWord(HearthRefusalRetry("character is on a transport")), "elsewhere");
    CheckWord("already home is answered by walking away or binding elsewhere",
              TownRetryWord(HearthRefusalRetry("home is where the character already stands")),
              "elsewhere");

    // AN INSTANCE IS DELIBERATELY NOT A WALL, AND THIS IS THE LINE THAT SAYS
    // SO. #286 refuses a bind inside one because SendBindPoint returns without
    // doing anything there. Nothing on the hearthstone's path does that:
    // SPELL_EFFECT_TELEPORT_UNITS has no case in Spell::CheckCast's effect
    // switch, and SpellInfo::CheckLocation only bars a spell carrying
    // SPELL_ATTR6_NOT_IN_RAID_INSTANCES, which this one does not. Hearthing out
    // of a dungeon is how a player leaves one. The two verbs differ here, and
    // copying bind's refusal across would have been this module inventing a
    // rule the game does not have.
    //
    // It falls through to `later` as an unrecognised literal, which is the
    // correct answer for a string this executor never returns.
    CheckWord("an instance is not a wall for a hearth",
              TownRetryWord(HearthRefusalRetry("character is inside an instance")), "later");

    // LATER: the character's own state is the wall, and every one of these ends
    // on its own.
    CheckWord("a fight ends", TownRetryWord(HearthRefusalRetry("character is in combat")),
              "later");
    CheckWord("a corpse run ends", TownRetryWord(HearthRefusalRetry("character is dead")),
              "later");
    CheckWord("a flight lands", TownRetryWord(HearthRefusalRetry("character is in flight")),
              "later");
    CheckWord("a walk stops", TownRetryWord(HearthRefusalRetry("character is moving")),
              "later");
    CheckWord("a cast finishes",
              TownRetryWord(HearthRefusalRetry("character is already casting")), "later");
    CheckWord("an hour passes",
              TownRetryWord(HearthRefusalRetry("hearthstone is on cooldown")), "later");
    CheckWord("a mind control ends",
              TownRetryWord(HearthRefusalRetry("character is not its own mover")), "later");
    // THE REFUSAL THAT COMES FROM THE TELEPORT AND NOT FROM THE CAST. A far
    // teleport waits on MSG_MOVE_WORLDPORT_ACK, which a client sends and a bot
    // does not have. mod-playerbots answers it from PlayerbotAI::HandleTeleportAck
    // every tick, so a bot-driven character is fine and a character with no bot
    // AI would wedge mid-teleport with nothing in the world to finish it. That
    // is worse than any refusal, so it is refused.
    CheckWord("a bot AI may still attach",
              TownRetryWord(HearthRefusalRetry(
                  "character has no bot AI to acknowledge the teleport")),
              "later");

    // A refusal this table has never heard of is more likely a new transient
    // than a new permanent, which is the call the bind, sell and repair tables
    // all make.
    CheckWord("an unknown refusal is worth asking again",
              TownRetryWord(HearthRefusalRetry("something nobody has written down yet")),
              "later");
}

void TheOutcomeWordsAreTheOnesARowCarries()
{
    CheckWord("arrived", HearthOutcomeWord(HearthOutcome::Arrived), "arrived");
    CheckWord("stayed", HearthOutcomeWord(HearthOutcome::Stayed), "stayed");
    CheckWord("elsewhere", HearthOutcomeWord(HearthOutcome::Elsewhere), "elsewhere");
    CheckWord("unreadable", HearthOutcomeWord(HearthOutcome::Unreadable), "unreadable");
}

}  // namespace

int main()
{
    TheOnlyFormIsUseAndNothingAtAllMeansIt();
    ArrivingIsTheOnlySuccess();
    NeverHavingLeftIsTheFailureThisExistsToSee();
    MovingSomewhereNobodyAskedForIsItsOwnAnswer();
    AReadingNobodyTookIsNoneOfTheThree();
    AStartLineThatIsAlsoTheFinishCannotBeJudged();
    TheWaitComesFromTheSpellAndNotFromAConstant();
    EveryRefusalCarriesWhereToTryAgain();
    TheOutcomeWordsAreTheOnesARowCarries();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok: a hearth is judged by where the character reads back, and the wait "
                "comes from the spell\n");
    return EXIT_SUCCESS;
}
