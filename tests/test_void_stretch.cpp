// A foot walk across the recorded no-ground stretch is refused (#1534).
#include <cstdlib>
#include <iostream>

#include "overseer_decisions.h"

using namespace OverseerDecisions;

static int failures = 0;
static void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

int main()
{
    // The two recorded fall deaths are fixtures.
    float const ax = -8501.0f, ay = -820.0f, bx = -8216.0f, by = -562.0f;
    // A walk from Burning Steppes side to Stormwind side crossing the middle.
    Check(!VoidStretchRefusal(0, -8000.0f, -900.0f, -8700.0f, -500.0f, true).empty(),
          "a line across the stretch is refused");
    Check(!VoidStretchRefusal(0, ax, ay, -9000.0f, -1200.0f, true).empty(),
          "a line starting at a death point is refused");
    Check(!VoidStretchRefusal(0, -7000.0f, 0.0f, bx, by, true).empty(),
          "a line ending at a death point is refused");
    Check(VoidStretchRefusal(0, -8000.0f, -2500.0f, -9000.0f, -2500.0f, true).empty(),
          "a line far south of it is not refused");
    Check(VoidStretchRefusal(1, -8000.0f, -900.0f, -8700.0f, -500.0f, true).empty(),
          "another map is not refused");
    Check(VoidStretchRefusal(0, -8000.0f, -900.0f, -8700.0f, -500.0f, false).empty(),
          "a trip not on foot is not refused");
    if (failures)
        return EXIT_FAILURE;
    std::cout << "void stretch: all checks passed\n";
    return EXIT_SUCCESS;
}
