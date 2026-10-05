/*
 * The faction's public channel: where a `chat` row on the `lfg` channel is
 * said (wow-overseer issue 591).
 *
 * A guild group short a real tank or healer asks the server for one the way
 * a player does: in LookingForGroup when the speaker is on it, else in the
 * General channel of its zone. A speaker with neither is refused rather than
 * reported delivered, because the core's Channel::Say tells a non-member
 * "not on channel" and says nothing.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using OverseerDecisions::GroupChatRouteFor;
using OverseerDecisions::PickPublicChannel;
using OverseerDecisions::PUBLIC_CHAT_CHANNEL;
using OverseerDecisions::PublicChannel;

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

void LookingForGroupFirst()
{
    Check("LookingForGroup when on it, whatever the zone",
          PickPublicChannel(true, true) == PublicChannel::LookingForGroup);
    Check("LookingForGroup in a zone with no General",
          PickPublicChannel(true, false) == PublicChannel::LookingForGroup);
}

void TheZonesGeneralOtherwise()
{
    Check("the zone's General when not on LookingForGroup",
          PickPublicChannel(false, true) == PublicChannel::ZoneGeneral);
}

void NeitherIsRefused()
{
    Check("nowhere to say it", PickPublicChannel(false, false) == PublicChannel::None);
}

void TheTokenIsNotAGroupChannel()
{
    Check("the token is lfg", std::string(PUBLIC_CHAT_CHANNEL) == "lfg");
    Check("lfg is not party or raid", !GroupChatRouteFor(PUBLIC_CHAT_CHANNEL).group);
}

}  // namespace

int main()
{
    LookingForGroupFirst();
    TheZonesGeneralOtherwise();
    NeitherIsRefused();
    TheTokenIsNotAGroupChannel();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_public_channel: ok\n");
    return 0;
}
