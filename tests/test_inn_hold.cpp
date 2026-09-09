/*
 * Whether a character walked to an inn is held there long enough to be bound.
 *
 * The live failure this pins is a campaign that cannot start. The coordinator
 * will not open a dungeon run until every member is bound at the inn its
 * dungeon is approached from, and two members spent twelve minutes each failing
 * to be bound, one after the other, indefinitely. Sampled on the dev realm from
 * forced saves rather than from stale rows, distance to the innkeeper in yards:
 *
 *     one member    3    26   265   144   160   296   783
 *     another     261   164   146    99   406   333   959
 *
 * One of them got to THREE YARDS of the innkeeper and was not bound. Neither
 * settled anywhere.
 *
 * The bind is only ever attempted on a poll that already finds the character
 * inside the arrival radius, and that poll is the party's own thirty seconds.
 * Nothing kept a character inside a five yard circle for thirty seconds, so the
 * two almost never coincided. The travel drive's escort arrival branch says an
 * arrived escort "holds there", and what it actually does is fall through and
 * let the walk be re-issued - which is a race, because upstream ends the walk on
 * arrival whether or not this module releases the errand, and a committed far
 * move is not clobbered by the next re-issue while it still has ten yards to
 * run.
 *
 * That race had already been measured and answered once, at the module's other
 * arrival point: four members at a dungeon door reading 42/47/44/45 yards out,
 * then 142/142/139/139, then 173/171/175/178, then 103/100/102/104, then
 * 214/215/215/214, never settling, three runs in a row. The answer was a hold,
 * on the argument that taking the mover off is not a competition. It was
 * applied at exactly one of the two places that needed it.
 *
 * So this is that hold's decision at the other place, and the input that makes
 * it safe: the arrival radius is measured against a point recorded in this
 * module, and the bind is gated on the core's own interact check against the
 * live creature, which has feet. A hold taken where those two disagree would
 * pin a character somewhere it can never be bound.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::InnHold;
using OverseerDecisions::InnHoldStep;

namespace
{

int failures = 0;

char const* Name(InnHold hold)
{
    switch (hold)
    {
        case InnHold::Nothing: return "nothing";
        case InnHold::Take:    return "take";
        case InnHold::Release: return "release";
    }
    return "unknown";
}

void CheckHold(char const* what, InnHold got, InnHold want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, Name(got), Name(want));
    ++failures;
}

// THE ONE THE ISSUE IS ABOUT. A character standing where it was walked to, with
// no refusal against it, is stood still. Without this it is walked at again
// every five seconds and wanders between the thirty second polls that could
// have bound it.
void ACharacterStandingAtItsInnIsHeldThere()
{
    CheckHold("standing there, nothing refused", InnHoldStep(true, false, false),
              InnHold::Take);
}

// ...AND THE HOLD IS RE-ASSERTED RATHER THAN ASSUMED TO STILL BE THERE. The
// register stops this module's own sweeps and nothing else, so a strategy
// something outside it granted has to be taken off again. Placing and
// re-asserting are one call on purpose, so there is one answer for both.
void AHoldAlreadyInForceIsStillTaken()
{
    CheckHold("standing there, already held", InnHoldStep(true, false, true),
              InnHold::Take);
}

// THE SAFETY INPUT, AND THE REASON THIS IS A DECISION AND NOT AN `IF`. Standing
// at the recorded point is exactly the state in which a refusal is most
// tempting to ignore, and exactly the state in which standing still cannot
// work: the bind was just attempted from there and the core turned it down.
// Pinning the character there would be this fix removing the only way it could
// ever have got lucky.
void ARefusedBindLetsGoEvenWhileStandingThere()
{
    CheckHold("standing there, bind refused, held", InnHoldStep(true, true, true),
              InnHold::Release);
}

// ...AND SAYS NOTHING WHEN THERE IS NOTHING TO LET GO OF. Releasing a hold that
// was never taken is a no-op with a log line attached, and a log line every poll
// about a character nothing is holding is how a real one gets buried.
void ARefusedBindWithNoHoldIsSilent()
{
    CheckHold("standing there, bind refused, not held", InnHoldStep(true, true, false),
              InnHold::Nothing);
}

// A CHARACTER THAT IS NOT THERE IS NOT BEING HELD THERE. `stay` is a non-combat
// strategy, so a held character still fights and still flees, and it comes out
// of a fight wherever the fight took it. This is the release that lets the walk
// start again.
void ACharacterDraggedOffItsInnIsLetGo()
{
    CheckHold("elsewhere, held", InnHoldStep(false, false, true), InnHold::Release);
    CheckHold("elsewhere, held, and refused too", InnHoldStep(false, true, true),
              InnHold::Release);
}

// AND A CHARACTER STILL WALKING IN IS LEFT ALONE, which is the ordinary poll for
// most of the trip and must cost nothing and say nothing.
void ACharacterStillWalkingInIsLeftAlone()
{
    CheckHold("elsewhere, not held", InnHoldStep(false, false, false), InnHold::Nothing);
    CheckHold("elsewhere, not held, refused", InnHoldStep(false, true, false),
              InnHold::Nothing);
}

// THE WHOLE TRUTH TABLE, WRITTEN OUT. Three booleans is eight rows and there is
// no reason to leave any of them to inference: this decides whether a character
// can be stopped long enough to be bound, and a campaign that cannot start is
// what the wrong answer costs.
void EveryCombinationIsWhatItSays()
{
    struct Row { bool at; bool refused; bool held; InnHold want; char const* what; };
    Row const rows[] = {
        {false, false, false, InnHold::Nothing, "walking in"},
        {false, false, true,  InnHold::Release, "dragged off the inn"},
        {false, true,  false, InnHold::Nothing, "walking in, refused last time"},
        {false, true,  true,  InnHold::Release, "dragged off, refused last time"},
        {true,  false, false, InnHold::Take,    "ARRIVED and nothing refused - the defect"},
        {true,  false, true,  InnHold::Take,    "arrived, hold re-asserted"},
        {true,  true,  false, InnHold::Nothing, "arrived but the bind was refused here"},
        {true,  true,  true,  InnHold::Release, "arrived, refused, and let go"},
    };
    for (Row const& row : rows)
        CheckHold(row.what, InnHoldStep(row.at, row.refused, row.held), row.want);
}

// A REFUSAL NEVER PRODUCES A HOLD, whatever else is true. This is the property
// the whole safety argument rests on, so it is asserted as a property rather
// than left to be read out of the table above.
void NoRefusedBindEverTakesAHold()
{
    for (int at = 0; at < 2; ++at)
        for (int held = 0; held < 2; ++held)
            if (InnHoldStep(at != 0, true, held != 0) == InnHold::Take)
            {
                std::printf("FAIL a refused bind took a hold (at=%d held=%d)\n", at, held);
                ++failures;
            }
}

// AND NOTHING IS EVER RELEASED THAT WAS NOT HELD, which is the other half: the
// release path writes a log line and hands strategies back, and doing either for
// a character this module is not holding would be reporting work it did not do.
void NothingUnheldIsEverReleased()
{
    for (int at = 0; at < 2; ++at)
        for (int refused = 0; refused < 2; ++refused)
            if (InnHoldStep(at != 0, refused != 0, false) == InnHold::Release)
            {
                std::printf("FAIL an unheld character was released (at=%d refused=%d)\n",
                            at, refused);
                ++failures;
            }
}

}  // namespace

int main()
{
    ACharacterStandingAtItsInnIsHeldThere();
    AHoldAlreadyInForceIsStillTaken();
    ARefusedBindLetsGoEvenWhileStandingThere();
    ARefusedBindWithNoHoldIsSilent();
    ACharacterDraggedOffItsInnIsLetGo();
    ACharacterStillWalkingInIsLeftAlone();
    EveryCombinationIsWhatItSays();
    NoRefusedBindEverTakesAHold();
    NothingUnheldIsEverReleased();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a character that will not stand still cannot be bound\n");
    return EXIT_SUCCESS;
}
