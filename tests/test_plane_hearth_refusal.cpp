/*
 * A plane hearth refused for a reason that clears by itself is not an error.
 *
 * The hearth off the hidden plane is asked again on the next poll, and the hold
 * DoHearth places is what clears "character is moving". Logged at ERROR, that
 * one transient refusal read as a stuck character when the cast landed seconds
 * later. A refusal that will not clear by itself stays loud.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

using OverseerDecisions::PlaneHearthRefusalClearsItself;

namespace
{
int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}
}  // namespace

int main()
{
    Check(PlaneHearthRefusalClearsItself("character is moving"),
          "a moving refusal is cleared by the hold");
    Check(PlaneHearthRefusalClearsItself("character is already casting"),
          "a cast in progress ends by itself");
    Check(!PlaneHearthRefusalClearsItself("character is in combat"),
          "combat is not guessed to clear");
    Check(!PlaneHearthRefusalClearsItself("character is dead"), "death does not clear");
    Check(!PlaneHearthRefusalClearsItself("character is on a flight path"),
          "a flight is not guessed to clear");
    Check(!PlaneHearthRefusalClearsItself(""), "an empty reason is not a known transient");

    if (failures)
        return EXIT_FAILURE;
    std::printf("plane hearth refusal: ok\n");
    return EXIT_SUCCESS;
}
