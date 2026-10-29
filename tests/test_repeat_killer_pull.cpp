/*
 * A creature entry that killed a member twice within an hour is not pulled
 * again by the module (mod-overseer#151), and a member it attacks still
 * fights back.
 *
 * The pull the module starts on its own is the hunt's (HuntGate / HuntNext);
 * the death memory is CreatureDeathBook. Compiled against
 * src/overseer_decisions.cpp; the wiring checks read src/mod_overseer.cpp.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

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

std::string ReadFile(char const* path)
{
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

constexpr uint32_t ENTRY = 1234;

D::HuntFacts FactsFor(D::CreatureDeathBook const& book, uint32_t entry)
{
    D::HuntFacts f;
    f.entrySuppressed = D::CreaturePullIsSuppressed(book, entry);
    return f;
}

void TwoDeathsWithinTheHourStopTheVoluntaryPull()
{
    D::CreatureDeathBook book;
    Check("fresh book pulls", !*D::HuntGate(FactsFor(book, ENTRY)));

    D::CreatureDeathNote(book, ENTRY, 1000);
    Check("one death still pulls", !*D::HuntGate(FactsFor(book, ENTRY)));

    D::CreatureDeathNote(book, ENTRY, 1000 + 1800);
    Check("two deaths in the hour are suppressed", D::CreaturePullIsSuppressed(book, ENTRY));
    Check("the gate refuses by name",
          std::strcmp(D::HuntGate(FactsFor(book, ENTRY)), D::HuntRefusal::RepeatKiller) == 0);

    // The poll never pulls it either: it waits.
    D::HuntPollFacts poll;
    poll.gate = FactsFor(book, ENTRY);
    Check("the hunt poll waits, no pull", D::HuntNext(poll) == D::HuntStep::Wait);

    // For the rest of the session: an hour later it is still listed.
    D::CreatureDeathNote(book, 99, 1000 + 20000);
    Check("still suppressed much later", D::CreaturePullIsSuppressed(book, ENTRY));
    Check("the refusal is worth asking again later",
          D::HuntRefusalRetry(D::HuntRefusal::RepeatKiller) == D::TownRetry::Later);
}

void OtherEntriesAndSlowDeathsDoNotCount()
{
    D::CreatureDeathBook book;
    D::CreatureDeathNote(book, ENTRY, 1000);
    D::CreatureDeathNote(book, 4321, 1100);
    Check("one death each is not a repeat", !D::CreaturePullIsSuppressed(book, ENTRY) &&
                                                !D::CreaturePullIsSuppressed(book, 4321));

    D::CreatureDeathNote(book, ENTRY, 1000 + 3601);
    Check("two deaths 3601 s apart are not a repeat",
          !D::CreaturePullIsSuppressed(book, ENTRY));
    D::CreatureDeathNote(book, ENTRY, 1000 + 3601 + 60);
    Check("the newer pair inside the hour is", D::CreaturePullIsSuppressed(book, ENTRY));

    Check("entry 0 is never a creature", !D::CreatureDeathNote(book, 0, 5000) &&
                                             !D::CreaturePullIsSuppressed(book, 0));
}

void TheHourIsPinnedWithLiterals()
{
    Check("the window is 3600 s", D::REPEAT_KILLER_WINDOW_SECONDS == 3600);
    Check("the count is 2", D::REPEAT_KILLER_DEATHS == 2);

    D::CreatureDeathBook exact;
    D::CreatureDeathNote(exact, ENTRY, 5000);
    D::CreatureDeathNote(exact, ENTRY, 5000 + 3600);
    Check("a pair exactly 3600 s apart counts", D::CreaturePullIsSuppressed(exact, ENTRY));

    D::CreatureDeathBook over;
    D::CreatureDeathNote(over, ENTRY, 5000);
    D::CreatureDeathNote(over, ENTRY, 5000 + 3601);
    Check("a pair 3601 s apart does not", !D::CreaturePullIsSuppressed(over, ENTRY));

    D::CreatureDeathBook far;
    D::CreatureDeathNote(far, ENTRY, 5000);
    D::CreatureDeathNote(far, ENTRY, 5000 + 7200);
    Check("a pair 7200 s apart does not", !D::CreaturePullIsSuppressed(far, ENTRY));

    D::CreatureDeathBook same;
    D::CreatureDeathNote(same, ENTRY, 5000);
    D::CreatureDeathNote(same, ENTRY, 5000);
    Check("two deaths at one timestamp count", D::CreaturePullIsSuppressed(same, ENTRY));

    // Deaths are counted per entry, not per member: the book is not told which
    // member died, so one member dying twice and two members dying once each
    // are the same two notes.
    D::CreatureDeathBook two;
    D::CreatureDeathNote(two, ENTRY, 100);
    D::CreatureDeathNote(two, ENTRY, 200);
    Check("two notes list the entry whoever died", D::CreaturePullIsSuppressed(two, ENTRY));
}

void AttackBackIsUnchanged()
{
    D::CreatureDeathBook book;
    D::CreatureDeathNote(book, ENTRY, 1000);
    D::CreatureDeathNote(book, ENTRY, 1500);
    Check("suppressed", D::CreaturePullIsSuppressed(book, ENTRY));

    // A member the entry attacks is already in combat: the hunt poll hands
    // that to the bot's own AI (Fight), suppressed or not.
    D::HuntPollFacts poll;
    poll.gate = FactsFor(book, ENTRY);
    poll.inCombat = true;
    Check("attacked member keeps fighting", D::HuntNext(poll) == D::HuntStep::Fight);

    // The entry flag is read only by the pull gate, not by the combat step.
    poll.gate.entrySuppressed = false;
    Check("same step without the flag", D::HuntNext(poll) == D::HuntStep::Fight);
}

void TheAdapterRecordsDeathsAndReadsTheBookAtTheGate()
{
    std::string const src = ReadFile("src/mod_overseer.cpp");
    Check("source readable", !src.empty());
    Check("a creature kill is noted",
          src.find("CreatureDeathNote(g_creatureDeathBook, killerEntry, now)") != std::string::npos);
    Check("the hunt facts read the book",
          src.find("CreaturePullIsSuppressed(g_creatureDeathBook, entry)") != std::string::npos);
}

}  // namespace

int main()
{
    TwoDeathsWithinTheHourStopTheVoluntaryPull();
    OtherEntriesAndSlowDeathsDoNotCount();
    TheHourIsPinnedWithLiterals();
    AttackBackIsUnchanged();
    TheAdapterRecordsDeathsAndReadsTheBookAtTheGate();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_repeat_killer_pull: ok\n");
    return 0;
}
