/*
 * The give backoff, decided without a world.
 *
 * WHAT IT IS FOR. A give that cannot succeed is asked again on every drain of
 * the command queue, because the sender re-inserts anything it has not seen
 * succeed and the queue drains every two seconds. Measured on the dev world for
 * mod-overseer#169: 31 rows of one identical refusal, the oldest hours old,
 * against three delivered gives in the same table. The cost is not the work, it
 * is that every one of those attempts is a fresh failure with a fresh answer,
 * and everything downstream that reacts to an answer reacts again.
 *
 * WHAT IS PINNED HERE, and why each is worth a case of its own:
 *
 *   - It is a BACKOFF and not a give-up. A wall that is still standing is
 *     re-tested, just on a cadence a person would use rather than the queue's.
 *     A rule that stopped retrying entirely would turn a transient refusal - a
 *     fight, a bag being filled and emptied - into a permanent one.
 *   - "Say it once" and "try it rarely" are SEPARATE answers. The reason is
 *     printed when it is new and not when it repeats, while the clock restarts
 *     on every attempt. Collapsing the two either floods the log or lets the
 *     backoff lapse into a two-second retry again.
 *   - A refusal that outlives its reason is its own bug, so the book forgets.
 *     Nothing else walks it, which makes the ask the only place a sweep can
 *     happen, and a book that only ever grows is a leak with a slow fuse.
 *
 * Compiled against src/overseer_decisions.cpp and NOTHING ELSE, like its
 * sibling: if a core type ever gets into one of these decisions this stops
 * building, which is the property the header says it is protecting.
 */

#include "overseer_decisions.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>

using OverseerDecisions::GiveHeldOff;
using OverseerDecisions::GiveRefusalBook;
using OverseerDecisions::NoteGiveRefusal;

namespace
{

// The two constants mod_overseer.cpp passes in, named here so a failure reads
// as the rule rather than as a pair of numbers.
time_t const BACKOFF = 60;
time_t const FORGET = 30 * 60;

int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void CheckSize(char const* what, std::size_t got, std::size_t want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: book holds %zu, wanted %zu\n", what, got, want);
    ++failures;
}

void CheckReason(char const* what, std::string const& got, char const* want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: reason '%s', wanted '%s'\n", what, got.c_str(), want);
    ++failures;
}

std::string const KEY = "Grog\nguid:1303004";
char const* const FULL = "receiver bags are full";

// An unremembered give is never held: the first attempt always happens.
void AFreshGiveIsAlwaysTried()
{
    GiveRefusalBook book;
    std::string reason;
    Check("a give nobody has refused is tried", GiveHeldOff(book, KEY, 1000, BACKOFF, FORGET, reason), false);
    CheckReason("and carries no remembered reason", reason, "");
}

// The refusal is worth saying the first time and not the second, and the row
// can still be answered from the memory while the pause runs.
void TheReasonIsSaidOnceAndThenHeld()
{
    GiveRefusalBook book;
    std::string reason;

    Check("the first refusal is worth saying", NoteGiveRefusal(book, KEY, FULL, 1000), true);

    Check("the next poll is held", GiveHeldOff(book, KEY, 1002, BACKOFF, FORGET, reason), true);
    CheckReason("with the reason it was refused for", reason, FULL);

    Check("and so is the poll after that", GiveHeldOff(book, KEY, 1030, BACKOFF, FORGET, reason), true);
}

// The pause is a pause. Past it the give is attempted again, and refusing it
// again restarts the clock rather than letting it retry every two seconds.
void ThePauseLapsesAndThenRestarts()
{
    GiveRefusalBook book;
    std::string reason;

    NoteGiveRefusal(book, KEY, FULL, 1000);
    Check("one second before the backoff, still held",
          GiveHeldOff(book, KEY, 1000 + BACKOFF - 1, BACKOFF, FORGET, reason), true);
    Check("at the backoff, tried again",
          GiveHeldOff(book, KEY, 1000 + BACKOFF, BACKOFF, FORGET, reason), false);

    Check("the same wall again is not worth saying twice",
          NoteGiveRefusal(book, KEY, FULL, 1000 + BACKOFF), false);
    Check("but it buys another pause",
          GiveHeldOff(book, KEY, 1000 + BACKOFF + 1, BACKOFF, FORGET, reason), true);
}

// A refusal for a DIFFERENT reason is a change, and a change is worth a line
// even though the outcome is the same refusal.
void ANewReasonIsWorthSayingAgain()
{
    GiveRefusalBook book;

    Check("the first reason is said", NoteGiveRefusal(book, KEY, FULL, 1000), true);
    Check("a different reason is said too",
          NoteGiveRefusal(book, KEY, "receiver not online", 1000 + BACKOFF), true);
    Check("and then not again",
          NoteGiveRefusal(book, KEY, "receiver not online", 1000 + 2 * BACKOFF), false);
}

// One give's pause says nothing about another's.
void OneGiveDoesNotHoldAnother()
{
    GiveRefusalBook book;
    std::string reason;

    NoteGiveRefusal(book, KEY, FULL, 1000);
    Check("a different give is not held",
          GiveHeldOff(book, "Grug\nguid:1304908", 1002, BACKOFF, FORGET, reason), false);
    CheckReason("and borrows no reason from it", reason, "");
}

// The ask is the only walk of the book, so it is the only place a sweep can
// happen - including for entries nobody is asking about.
void ColdEntriesAreForgotten()
{
    GiveRefusalBook book;
    std::string reason;

    NoteGiveRefusal(book, KEY, FULL, 1000);
    NoteGiveRefusal(book, "Grug\nguid:1304908", FULL, 1000);
    CheckSize("both refusals are remembered", book.size(), 2);

    // Past the backoff and short of the forget: still there, no longer holding.
    Check("a lapsed entry does not hold",
          GiveHeldOff(book, KEY, 1000 + FORGET - 1, BACKOFF, FORGET, reason), false);
    CheckSize("and is still remembered", book.size(), 2);

    // Past the forget: swept, whichever key was asked about.
    Check("a cold entry does not hold either",
          GiveHeldOff(book, KEY, 1000 + FORGET, BACKOFF, FORGET, reason), false);
    CheckSize("and neither entry survives the sweep", book.size(), 0);
}

}  // namespace

int main()
{
    AFreshGiveIsAlwaysTried();
    TheReasonIsSaidOnceAndThenHeld();
    ThePauseLapsesAndThenRestarts();
    ANewReasonIsWorthSayingAgain();
    OneGiveDoesNotHoldAnother();
    ColdEntriesAreForgotten();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("the give backoff holds\n");
    return EXIT_SUCCESS;
}
