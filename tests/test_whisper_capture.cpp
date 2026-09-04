/*
 * Whisper capture has one listener, never a sender self-echo (#22).
 *
 * Compiled against the pure decision file and nothing from AzerothCore. The
 * chat hook supplies the three GUID counters; this test pins the identity
 * rule without needing a worldserver or database.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::WhisperWatcherIsGenuineListener;

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

void OnlyTheReceiverIsRecorded()
{
    Check("receiver is a genuine listener",
          WhisperWatcherIsGenuineListener(10, 20, 20), true);
    Check("sender self-echo is rejected",
          WhisperWatcherIsGenuineListener(10, 20, 10), false);
    Check("third party is rejected",
          WhisperWatcherIsGenuineListener(10, 20, 30), false);
}

void MissingReceiverIsRejected()
{
    Check("missing receiver is not a listener",
          WhisperWatcherIsGenuineListener(10, 0, 10), false);
    Check("sender and receiver cannot be the same listener",
          WhisperWatcherIsGenuineListener(10, 10, 10), false);
}

}  // namespace

int main()
{
    OnlyTheReceiverIsRecorded();
    MissingReceiverIsRejected();
    return failures ? 1 : 0;
}
