/*
 * A follower is lent `new rpg` to board its leader's flight, and gives it back
 * on landing.
 *
 * Measured 2026-09-23 on the dev realm. Every party flight was refused with
 * "'Zrog' is not a character this module steers - walking": a follower does
 * not carry `new rpg`, and boarding is that engine's action. So the leader
 * walked 5,000 yards, or flew alone and stranded the family.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::FlightLoanStep;
using OverseerDecisions::ReadFlightLoan;

namespace
{

int failures = 0;
constexpr time_t LIMIT = 5 * 60;

void Check(char const* what, FlightLoanStep got, FlightLoanStep want)
{
    if (got == want)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

}  // namespace

int main()
{
    Check("no loan, nothing to do", ReadFlightLoan(false, false, true, 0, 100, LIMIT),
          FlightLoanStep::Keep);
    Check("walking to the flight master keeps the loan",
          ReadFlightLoan(true, false, false, 1000, 1060, LIMIT), FlightLoanStep::Keep);
    Check("taking off is remembered", ReadFlightLoan(true, false, true, 1000, 1090, LIMIT),
          FlightLoanStep::Flying);
    Check("in the air keeps the loan", ReadFlightLoan(true, true, true, 1000, 1200, LIMIT),
          FlightLoanStep::Keep);
    Check("landing gives it back", ReadFlightLoan(true, true, false, 1000, 1400, LIMIT),
          FlightLoanStep::Return);
    Check("never taking off gives it back after the limit",
          ReadFlightLoan(true, false, false, 1000, 1000 + LIMIT + 1, LIMIT),
          FlightLoanStep::Return);
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
