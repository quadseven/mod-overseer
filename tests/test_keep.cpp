/*
 * overseer_keep: an item a character must keep, and where it waits.
 *
 * THE CASE IS THE DEV REALM'S. A paladin wears an epic sword that needs level
 * 52. The operator is lowering him to his natural level, and he keeps the
 * sword: taken off at once, banked at the next banker, never sold, given,
 * mailed, destroyed or auctioned, and worn again once he is back at 52.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using OverseerDecisions::KEEP_REFUSAL;
using OverseerDecisions::KeepPlace;
using OverseerDecisions::KeepReservation;
using OverseerDecisions::KeepReservationCovers;
using OverseerDecisions::KeepReservationFor;
using OverseerDecisions::KeepStep;
using OverseerDecisions::KeepStepFor;
using OverseerDecisions::KeepStepWord;

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

void TestCovers()
{
    KeepReservation const sword{"Grog", 647, 4909901, 52, "the operator keeps his sword"};
    Check("the instance is covered", KeepReservationCovers(sword, "Grog", 647, 4909901));
    Check("another instance of the entry is not, when a guid is named",
          !KeepReservationCovers(sword, "Grog", 647, 12345));
    Check("another character's is not", !KeepReservationCovers(sword, "Grug", 647, 4909901));

    KeepReservation const byEntry{"Grog", 647, 0, 0, ""};
    Check("an entry with no guid covers every instance", KeepReservationCovers(byEntry, "Grog", 647, 1) &&
                                                             KeepReservationCovers(byEntry, "Grog", 647, 2));
    Check("...and nothing else", !KeepReservationCovers(byEntry, "Grog", 648, 1));

    KeepReservation const empty{"Grog", 0, 0, 0, ""};
    Check("a reservation naming nothing covers nothing", !KeepReservationCovers(empty, "Grog", 0, 0) &&
                                                             !KeepReservationCovers(empty, "Grog", 647, 1));

    std::vector<KeepReservation> const all = {byEntry, sword};
    Check("the first covering reservation is the one", KeepReservationFor(all, "Grog", 647, 4909901) == &all[0]);
    Check("none covers an unreserved item", KeepReservationFor(all, "Grog", 2901, 1) == nullptr);
    Check("the refusal says why and carries no quote",
          std::strlen(KEEP_REFUSAL) > 0 && !std::strchr(KEEP_REFUSAL, '\''));
}

void TestSteps()
{
    // Lowered to 36 while wearing it.
    Check("worn below its level: taken off at once",
          KeepStepFor(36, 52, KeepPlace::Equipped, false) == KeepStep::Unequip);
    Check("in the bags with no banker: waits", KeepStepFor(36, 52, KeepPlace::Bags, false) == KeepStep::None);
    Check("in the bags at a banker: banked", KeepStepFor(36, 52, KeepPlace::Bags, true) == KeepStep::Deposit);
    Check("in the bank below its level: stays", KeepStepFor(51, 52, KeepPlace::Bank, true) == KeepStep::None);

    // Back at 52.
    Check("at its level, at a banker: out of the bank", KeepStepFor(52, 52, KeepPlace::Bank, true) == KeepStep::Withdraw);
    Check("at its level, no banker: stays banked", KeepStepFor(52, 52, KeepPlace::Bank, false) == KeepStep::None);
    Check("at its level in the bags: worn", KeepStepFor(52, 52, KeepPlace::Bags, false) == KeepStep::Equip);
    Check("worn at its level: nothing", KeepStepFor(60, 52, KeepPlace::Equipped, true) == KeepStep::None);

    Check("a keep with no level never moves", KeepStepFor(1, 0, KeepPlace::Equipped, true) == KeepStep::None &&
                                                  KeepStepFor(60, 0, KeepPlace::Bank, true) == KeepStep::None);
    Check("an item not on the character is not chased", KeepStepFor(36, 52, KeepPlace::Missing, true) == KeepStep::None);

    Check("step words", std::string(KeepStepWord(KeepStep::Unequip)) == "unequip" &&
                            std::string(KeepStepWord(KeepStep::Deposit)) == "deposit" &&
                            std::string(KeepStepWord(KeepStep::Withdraw)) == "withdraw" &&
                            std::string(KeepStepWord(KeepStep::Equip)) == "equip" &&
                            std::string(KeepStepWord(KeepStep::None)) == "none");
}

}  // namespace

int main()
{
    TestCovers();
    TestSteps();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_keep: all passed\n");
    return 0;
}
