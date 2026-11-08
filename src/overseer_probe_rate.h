#ifndef OVERSEER_PROBE_RATE_H
#define OVERSEER_PROBE_RATE_H

#include <ctime>

// The #1426 teleport probe says one line per character per this many seconds.
namespace OverseerDecisions
{
constexpr time_t PROBE_1426_SAY_SECONDS = 10;

// True when a line is due: nothing was said yet (lastSaid 0), or a full
// interval has passed. A clock that stepped back says again rather than stay
// silent for good.
inline bool ProbeLineDue(time_t now, time_t lastSaid)
{
    return lastSaid == 0 || now < lastSaid || now - lastSaid >= PROBE_1426_SAY_SECONDS;
}
}  // namespace OverseerDecisions

#endif
