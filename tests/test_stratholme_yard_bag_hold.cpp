/*
 * A family held by bag pressure still has its Stratholme yard episode polled
 * (#1221).
 *
 * A family that leaves Stratholme with full bags lands in the closed exit
 * yard while the bag-pressure answer is HoldOut. The hold returned before
 * DriveStratholmeYard was reached, so the yard episode was never polled: the
 * way out was never worked, and the episode closed only later, by its
 * 15-minute ceiling, with a warning that blamed the yard. An open episode is
 * now driven ahead of the hold; a family with no episode keeps the hold.
 *
 * The replay below walks one full-bags family through the coordinator's poll
 * order, poll by poll, asking the same decisions the adapter asks: the
 * bag-pressure answer, whether the yard drives before the hold, the yard box,
 * and DriveStratholmeYard's own close-or-drive step. The adapter's HoldOut
 * order and its use of that step are pinned by
 * test_stratholme_yard_bag_hold_source.cpp. Only the way out itself (walk,
 * button, hearth) is modelled, as a number of driven polls.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using OverseerDecisions::DungeonBagPressure;
using OverseerDecisions::DungeonRunBagPressure;
using OverseerDecisions::STRATHOLME_EXIT_YARD_MAP_ID;
using OverseerDecisions::STRATHOLME_YARD_EPISODE_SECONDS;
using OverseerDecisions::StratholmeMemberInYard;
using OverseerDecisions::StratholmeMemberPlace;
using OverseerDecisions::StratholmeYardDrivesBeforeBagHold;
using OverseerDecisions::StratholmeYardPoll;
using OverseerDecisions::StratholmeYardPollStep;

namespace
{
int failures = 0;

void CheckBool(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

enum class YardClose
{
    StillOpen,
    OwnPath,   // every member out of the yard
    Ceiling,   // the 15-minute warning
};

char const* Word(YardClose close)
{
    switch (close)
    {
        case YardClose::StillOpen: return "still open";
        case YardClose::OwnPath:   return "own path";
        case YardClose::Ceiling:   return "ceiling";
    }
    return "?";
}

constexpr std::uint32_t POLL_SECONDS = 10;
// Polls of the way out (walk, button, hearth) before the family stands
// outside the yard. Only a poll that drives the yard moves them.
constexpr unsigned DRIVEN_POLLS_TO_LEAVE = 12;
constexpr unsigned BAG_FLOOR = 3;

StratholmeMemberPlace Place(bool inYard)
{
    StratholmeMemberPlace place;
    place.found = true;
    place.inWorld = true;
    place.mapId = STRATHOLME_EXIT_YARD_MAP_ID;
    place.x = inYard ? 3210.f : 3300.f;
    place.y = -4045.f;
    return place;
}

// One family, every member with no free slot, from the poll it lands in the
// yard. `driveBeforeHold` false is the order on main: the hold returned first.
YardClose Replay(bool driveBeforeHold, std::uint32_t secondsUntilBagsFree)
{
    std::vector<unsigned> freeSlots{0, 0, 0, 0, 0};
    bool episodeOpen = true;
    unsigned drivenPolls = 0;
    for (std::uint32_t elapsed = 0; elapsed <= 2 * STRATHOLME_YARD_EPISODE_SECONDS;
         elapsed += POLL_SECONDS)
    {
        if (elapsed >= secondsUntilBagsFree)
            freeSlots.assign(freeSlots.size(), 10);
        bool const inYard = StratholmeMemberInYard(Place(drivenPolls < DRIVEN_POLLS_TO_LEAVE));

        // DriveStratholmeYard's own first question, asked the way it asks it.
        auto drive = [&]() -> YardClose {
            if (!episodeOpen)
                return YardClose::StillOpen;
            switch (StratholmeYardPollStep(inYard, elapsed))
            {
                case StratholmeYardPoll::EveryoneOut:
                    episodeOpen = false;
                    return YardClose::OwnPath;
                case StratholmeYardPoll::Ceiling:
                    episodeOpen = false;
                    return YardClose::Ceiling;
                case StratholmeYardPoll::Drive:
                    break;
            }
            ++drivenPolls;
            return YardClose::StillOpen;
        };

        // The yard is outside the instance and the run is not leaving.
        DungeonBagPressure const pressure =
            DungeonRunBagPressure(freeSlots, BAG_FLOOR, false, false);
        if (pressure == DungeonBagPressure::HoldOut)
        {
            bool const asked = driveBeforeHold
                                   ? StratholmeYardDrivesBeforeBagHold(episodeOpen)
                                   : false;
            if (asked)
            {
                YardClose const closed = drive();
                if (closed != YardClose::StillOpen)
                    return closed;
            }
            continue;  // the bag hold returns
        }
        YardClose const closed = drive();
        if (closed != YardClose::StillOpen)
            return closed;
    }
    return YardClose::StillOpen;
}

void CheckClose(char const* what, YardClose got, YardClose want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, want %s\n", what, Word(got), Word(want));
    ++failures;
}
}

int main()
{
    CheckBool("an open yard episode is driven before the bag hold returns",
              StratholmeYardDrivesBeforeBagHold(true), true);
    CheckBool("a family with no yard episode keeps the bag hold as it was",
              StratholmeYardDrivesBeforeBagHold(false), false);

    CheckBool("a yard with nobody in it closes on its own path",
              StratholmeYardPollStep(false, 0) == StratholmeYardPoll::EveryoneOut, true);
    CheckBool("nobody in the yard closes on its own path even past the ceiling",
              StratholmeYardPollStep(false, STRATHOLME_YARD_EPISODE_SECONDS) ==
                  StratholmeYardPoll::EveryoneOut,
              true);
    CheckBool("a member still in the yard at the ceiling gives up",
              StratholmeYardPollStep(true, STRATHOLME_YARD_EPISODE_SECONDS) ==
                  StratholmeYardPoll::Ceiling,
              true);
    CheckBool("a member in the yard before the ceiling is driven",
              StratholmeYardPollStep(true, STRATHOLME_YARD_EPISODE_SECONDS - 1) ==
                  StratholmeYardPoll::Drive,
              true);

    CheckBool("a full-bags family in the yard is answered HoldOut",
              DungeonRunBagPressure({0, 0, 0, 0, 0}, BAG_FLOOR, false, false) ==
                  DungeonBagPressure::HoldOut,
              true);

    // Bags stay full for the whole replay: the yard closes on its own path.
    CheckClose("a full-bags family leaves the yard and the episode closes on its own path",
               Replay(true, 0xFFFFFFFFu), YardClose::OwnPath);
    // The order on main: the episode is never polled while bags are full, and
    // the first poll after room comes back finds it past the ceiling.
    CheckClose("with the hold first the episode closes only by the ceiling",
               Replay(false, STRATHOLME_YARD_EPISODE_SECONDS + 5 * 60), YardClose::Ceiling);
    // No bag pressure: the yard is driven after the switch, as before.
    CheckClose("a family with room closes the yard on its own path as before",
               Replay(true, 0), YardClose::OwnPath);
    return failures ? 1 : 0;
}
