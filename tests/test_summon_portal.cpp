/*
 * The portal does not exist yet when the stone is clicked.
 *
 * mod-overseer#365. The summon verb clicked a meeting stone and looked for the
 * ritual object in the next statement, and the search could never find one. A
 * refused row on the dev realm, read out of the module's own result column:
 *
 *   reason                    the summoner did not begin channelling the portal
 *   stone_yards               1.43     helper_stone_yards   1.75
 *   walked_summoner  false    walked_helper  false    approach_ms  0
 *   channelling_after_click   true
 *   portal_entry              0        participants  0     required  0
 *
 * Both clickers are ON the stone against a gate of five, the approach walk was
 * never needed, the click worked, the core started a channel - and the verb
 * refused, naming the one thing that had gone right.
 *
 * WHAT THE CORE DOES, at the pinned SHA. GameObject::Use's MEETINGSTONE branch
 * (GameObject.cpp:1902) casts spell 23598 on the clicker. That spell is
 * channelled, which the row above proves rather than assumes: only
 * Spell::GetCurrentContainer (Spell.cpp:8021) puts a spell in
 * CURRENT_CHANNELED_SPELL, and that is the field the module read back true.
 * Spell::prepare casts immediately in exactly two places and a channelled spell
 * takes neither (Spell.cpp:3646, Spell.cpp:3691); what prepare does instead is
 * SetCurrentCastedSpell (Spell.cpp:3665). The cast is left to Spell::update
 * (Spell.cpp:4430), and only inside it does handle_immediate (Spell.cpp:4036)
 * reach EffectTransmitted (SpellEffects.cpp:5379), its
 * GAMEOBJECT_TYPE_SUMMONING_RITUAL branch (SpellEffects.cpp:5466) and AddToMap
 * (SpellEffects.cpp:5497). The module's own hook runs after the map update of
 * the same tick (World.cpp:1242 and World.cpp:1342), so the portal first exists
 * one world tick after the poll that clicked the stone.
 *
 * So the row waits, and what is pinned here is the decision that wait makes on
 * every poll: click it, wait for it, say the channel is gone, or give up. The
 * search itself is a cell visit and lives in the adapter, where it can be read
 * against the core; the rule about when to keep waiting is a decision and lives
 * here.
 *
 * The two new refusal literals are pinned too, because a refusal that names the
 * wrong cause is half of what this issue is about. Both are `later`: a channel
 * that ended and a portal that never appeared are each about a moment rather
 * than a place, and the same pair standing on the same stone is the right thing
 * to try next.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::ReadSummonPortal;
using OverseerDecisions::SummonPortal;
using OverseerDecisions::SummonPortalWord;
using OverseerDecisions::SummonRefusalRetry;
using OverseerDecisions::TownRetry;
using OverseerDecisions::TownRetryWord;

namespace
{

int failures = 0;

void CheckPortal(char const* what, SummonPortal got, SummonPortal want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, SummonPortalWord(got),
                SummonPortalWord(want));
    ++failures;
}

void CheckRetry(char const* what, TownRetry got, TownRetry want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, TownRetryWord(got), TownRetryWord(want));
    ++failures;
}

// The ceiling the adapter actually passes, spelled once so a case that means
// "six seconds" cannot be read as "some time".
constexpr uint32_t CEILING = 6000;

// ------------------------------------------------ the measured case first --

// THE POLL THE STONE WAS CLICKED ON, WHICH IS THE WHOLE ISSUE. The channel has
// started and the core has not cast the spell that makes the portal, so there
// is nothing to find and nothing has gone wrong. Every summon this module has
// ever driven died on this reading being treated as a failure.
void TheClickPollHasNoPortalAndThatIsNormal()
{
    CheckPortal("no portal on the click poll is a wait",
                ReadSummonPortal(false, true, 0, CEILING), SummonPortal::Wait);
    CheckPortal("and still a wait a poll later",
                ReadSummonPortal(false, true, 2000, CEILING), SummonPortal::Wait);
}

// AND THE POLL AFTER IT, WHICH IS WHAT THE FIX BUYS. One world tick is fifty
// milliseconds and this module polls every two seconds, so by the first poll
// that asks, the object is there.
void ThePortalArrivesAndIsClicked()
{
    CheckPortal("a portal in the world is clicked",
                ReadSummonPortal(true, true, 2000, CEILING), SummonPortal::Click);
}

// THE OBJECT BEATS EVERYTHING ELSE, and it is asked first for a reason: it is
// the only input here that is a fact about the ritual rather than about the
// summoner, and it is the thing that actually gets clicked. A portal found on
// the very poll the ceiling runs out is a summon that was about to work, and
// answering `OutOfTime` about it would throw one away.
void APortalFoundLateIsStillAPortal()
{
    CheckPortal("a portal on the last poll is not a timeout",
                ReadSummonPortal(true, true, CEILING, CEILING), SummonPortal::Click);
    CheckPortal("and a portal outlives a reading that says the channel is gone",
                ReadSummonPortal(true, false, CEILING + 5000, CEILING), SummonPortal::Click);
}

// THE CHANNEL IS ASKED BEFORE THE CLOCK. The ritual object belongs to the
// channel - the core hands it to the caster with AddGameObject
// (SpellEffects.cpp:5470) and takes it away when the spell is cancelled - so a
// summoner that has stopped channelling with no portal in the world is not a row
// that needs more time. Spending the rest of the window on it would hold a claim
// open to watch nothing happen, which is the shape of failure this verb keeps
// being asked not to have.
void AChannelThatEndedIsNotAWait()
{
    CheckPortal("no channel and no portal ends it",
                ReadSummonPortal(false, false, 0, CEILING), SummonPortal::NoChannel);
    CheckPortal("and it ends it early rather than at the ceiling",
                ReadSummonPortal(false, false, 100, CEILING), SummonPortal::NoChannel);
}

// AND THE CLOCK IS THE LAST WORD. Still channelling, still nothing in the world:
// something this module cannot see is in the way, and the honest answer is to
// say so with the wait in the row rather than hold the claim open.
void AWaitThatHasHadItsTimeEnds()
{
    CheckPortal("the ceiling ends the wait", ReadSummonPortal(false, true, CEILING, CEILING),
                SummonPortal::OutOfTime);
    CheckPortal("and anything past it too",
                ReadSummonPortal(false, true, CEILING + 2000, CEILING), SummonPortal::OutOfTime);
    // A ceiling of zero waits for nobody, which is the same property the walk's
    // own decision has: a caller that asks for no wait gets none rather than one
    // free poll.
    CheckPortal("a zero ceiling waits for nobody", ReadSummonPortal(false, true, 0, 0),
                SummonPortal::OutOfTime);
}

// EVERY WAIT ENDS, which is the property rather than the four cases above. A row
// parked on this decision holds two characters still and a claim open, so there
// must be no combination of readings that answers `Wait` for ever.
void EveryWaitEnds()
{
    for (int bits = 0; bits < 4; ++bits)
    {
        bool const portalSeen = (bits & 1) != 0;
        bool const channelling = (bits & 2) != 0;
        SummonPortal const answer = ReadSummonPortal(portalSeen, channelling, CEILING, CEILING);
        if (answer == SummonPortal::Wait)
        {
            std::printf("FAIL a wait at the ceiling never ends: portal %d channel %d\n",
                        portalSeen ? 1 : 0, channelling ? 1 : 0);
            ++failures;
        }
    }
    // ...and below the ceiling, waiting is only ever right while the thing that
    // makes a portal is still running.
    CheckPortal("waiting needs a live channel", ReadSummonPortal(false, false, 10, CEILING),
                SummonPortal::NoChannel);
}

// THE WORDS ARE THE ONES A ROW CARRIES, so a reader grepping a log for what the
// module said finds the same strings a test pins.
void TheWordsAreTheOnesARowCarries()
{
    struct Case
    {
        SummonPortal answer;
        char const* word;
    } const cases[] = {
        { SummonPortal::Click, "click" },
        { SummonPortal::Wait, "wait" },
        { SummonPortal::NoChannel, "no-channel" },
        { SummonPortal::OutOfTime, "out-of-time" },
    };
    for (Case const& c : cases)
    {
        if (std::string(SummonPortalWord(c.answer)) == c.word)
            continue;
        std::printf("FAIL the word for %s is %s\n", c.word, SummonPortalWord(c.answer));
        ++failures;
    }
}

// AND THE THREE LITERALS ASK FOR THE RIGHT THING. The one that was doing the
// work of three now describes only its own case, and the two the wait adds are
// both about a moment rather than a place: the same pair, on the same stone, is
// exactly what should be tried next.
void TheRefusalsAskForTheRightThing()
{
    CheckRetry("a click that started nothing is later",
               SummonRefusalRetry("the summoner did not begin channelling the portal"),
               TownRetry::Later);
    CheckRetry("a channel that ended is later",
               SummonRefusalRetry("the summoner stopped channelling before a portal appeared"),
               TownRetry::Later);
    CheckRetry("a portal that never appeared is later",
               SummonRefusalRetry("the summoner is channelling but no portal appeared"),
               TownRetry::Later);
    // ...and none of them is a place problem, which is the class that would send
    // an operator to move the party to another stone for a fault that has
    // nothing to do with where anybody is standing.
    CheckRetry("and the stone is still the right stone",
               SummonRefusalRetry("no meeting stone within reach of the summoner"),
               TownRetry::Elsewhere);
}

}  // namespace

int main()
{
    TheClickPollHasNoPortalAndThatIsNormal();
    ThePortalArrivesAndIsClicked();
    APortalFoundLateIsStillAPortal();
    AChannelThatEndedIsNotAWait();
    AWaitThatHasHadItsTimeEnds();
    EveryWaitEnds();
    TheWordsAreTheOnesARowCarries();
    TheRefusalsAskForTheRightThing();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the row waits for the portal the core has not made yet, and says which\n");
    return EXIT_SUCCESS;
}
