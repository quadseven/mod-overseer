#ifndef OVERSEER_BRD_WING_H
#define OVERSEER_BRD_WING_H

// BLACKROCK DEPTHS: A RUN THAT CAN OPEN THE SHADOWFORGE DOORS GOES ON INTO
// THE UPPER CITY (mod-overseer#1410).
//
// The dungeon brain splits map 230 into two wings, chosen per run: the
// Detention Block (`brd-db`, encounter bits 0 to 8, High Interrogator Gerstahn
// to Bael'Gar) and the Upper City (`brd-uc`, bits 9 to 18, the Shadowforge Lock
// to Emperor Dagran Thaurissan). A bare `dc on` latches `brd-db`, and so does a
// family the dungeon finder took in on row 30. Nothing named `brd-uc`, so every
// run stopped at Bael'Gar, read NotYet against the whole map's 19 bits, and
// stood until the clearing watchdog's stall, a refused `dc skip` and the
// brain's "All bosses cleared!" ended it.
//
// The Upper City's lock-680 doors (the Shadowforge Gates, the East Garrison
// Door, the Shadowforge Lock lever and the Lyceum) open for a member carrying
// the Shadowforge Key (11000), or one whose lockpicking reaches the lock's 250;
// the dungeon brain hands the click to that member either way (DcDoorOpener).
// The key comes from Dark Iron Legacy at the Shrine of Thaurissan inside the
// Detention Block (#1391). So once the Detention Block is credited:
//   - a party that can open those doors is sent `dc on brd-uc` and keeps
//     clearing; the run is then complete on the whole map's mask;
//   - a party that cannot is finished there and then, not five minutes later.
// A chain the brain does not take (it is still on `brd-db` after the issue) is
// tried BRD_UPPER_CITY_TRIES times in a row, BRD_UPPER_CITY_RETRY_SECONDS apart.
//
// A RUN IS NEVER CALLED COMPLETE FOR WHAT IT DID NOT KILL (#1490). Measured
// 2026-11-10: a run had 17 of the map's 19 encounters credited, the Upper City
// well under way, when a worldserver restart and a stretch of lost leader
// client made the arming drive send a bare `dc on` three more times. Each one
// put the brain back on `brd-db`, each was followed by a chain that WAS
// accepted and took, and the counter - never reset, so a count of every chain
// the run had ever sent rather than of chains in a row that did not take -
// reached its limit and ended the run 'complete' with Magmus and the Emperor
// alive. So:
//   - the count is of consecutive chains that did not take: a brain seen on
//     the Upper City resets it (BrdTriesAfterPoll);
//   - exhausting it ends nothing. The run reads NotYet on the whole map's mask
//     and the clearing watchdog ends it 'stalled', which is what it was;
//   - a Detention Block finish is only for a party that cannot open the doors
//     AND has credited nothing past Bael'Gar.
//
// No AzerothCore include: tests/test_brd_wing.cpp compiles this alone.

#include <cstdint>
#include <ctime>

namespace OverseerDecisions
{

constexpr std::uint32_t BRD_WING_MAP_ID = 230;
constexpr std::uint32_t BRD_SHADOWFORGE_KEY = 11000;
// Lock 680's lockpicking slot: a rogue at this skill opens what the key opens.
constexpr std::uint32_t BRD_UPPER_CITY_LOCKPICKING = 250;
// DungeonEncounter.dbc bits on map 230, normal difficulty, as mod-dungeon-clear's
// RegisterBlackrockDepthsWings splits them.
constexpr std::uint32_t BRD_DETENTION_BLOCK_MASK = 0x1FFu;  // bits 0-8
constexpr std::uint32_t BRD_UPPER_CITY_MASK = 0x7FE00u;     // bits 9-18
// The dungeon brain's wing token for the Upper City (`dc on <wing>`).
constexpr char const* BRD_UPPER_CITY_WING = "brd-uc";
constexpr unsigned BRD_UPPER_CITY_TRIES = 3;
constexpr std::time_t BRD_UPPER_CITY_RETRY_SECONDS = 60;

enum class BrdWingStep : std::uint8_t
{
    Nothing,               // not map 230, the Detention Block not done, or the
                           // brain already clears the Upper City
    ChainUpperCity,        // send `dc on brd-uc` this poll
    WaitChain,             // a chain was sent; give the brain until the retry
    FinishDetentionBlock,  // no key and nothing past Bael'Gar credited: the run is done
};

struct BrdWingFacts
{
    std::uint32_t mapId{0};
    // The dungeon brain's own expected mask ("dungeon clear expected encounter
    // mask"): the wing it clears. 0 when unread or the module is not loaded.
    std::uint32_t brainMask{0};
    // The instance's completed-encounter mask.
    std::uint32_t credited{0};
    // A party member in this instance can open the lock-680 doors: it carries
    // the Shadowforge Key or its lockpicking reaches BRD_UPPER_CITY_LOCKPICKING.
    bool doorsOpen{false};
    // How many times this run has sent the chain, and when it last did.
    unsigned tries{0};
    std::time_t lastTryAt{0};
    std::time_t now{0};
};

// A member opens the Upper City's doors with the key or the lockpicking.
inline bool BrdOpensUpperCity(std::uint32_t keys, std::uint32_t lockpicking)
{
    return keys >= 1 || lockpicking >= BRD_UPPER_CITY_LOCKPICKING;
}

inline bool BrdDetentionBlockDone(std::uint32_t credited)
{
    return (credited & BRD_DETENTION_BLOCK_MASK) == BRD_DETENTION_BLOCK_MASK;
}

// The brain clears the Detention Block and nothing past it: its mask is some
// part of bits 0-8 and none of the Upper City's.
inline bool BrdBrainOnDetentionBlock(std::uint32_t brainMask)
{
    return brainMask != 0 && (brainMask & ~BRD_DETENTION_BLOCK_MASK) == 0;
}

// The brain clears the Upper City (alone or with the Detention Block).
inline bool BrdBrainOnUpperCity(std::uint32_t brainMask)
{
    return (brainMask & BRD_UPPER_CITY_MASK) != 0;
}

// Any Upper City encounter credited: the party is, or was, past the doors.
inline bool BrdUpperCityStarted(std::uint32_t credited)
{
    return (credited & BRD_UPPER_CITY_MASK) != 0;
}

// The chain counter after one poll: a brain seen on the Upper City has taken
// the chain, so the count of chains that did not take starts over.
inline unsigned BrdTriesAfterPoll(unsigned tries, std::uint32_t brainMask)
{
    return BrdBrainOnUpperCity(brainMask) ? 0u : tries;
}

inline BrdWingStep BrdWingNext(BrdWingFacts const& f)
{
    if (f.mapId != BRD_WING_MAP_ID || !BrdDetentionBlockDone(f.credited))
        return BrdWingStep::Nothing;
    // On the Upper City (or an unread brain): the whole map's mask decides,
    // as on every other connected dungeon.
    if (!BrdBrainOnDetentionBlock(f.brainMask))
        return BrdWingStep::Nothing;
    if (!f.doorsOpen)
        return BrdUpperCityStarted(f.credited) ? BrdWingStep::Nothing
                                               : BrdWingStep::FinishDetentionBlock;
    if (f.tries == 0)
        return BrdWingStep::ChainUpperCity;
    if (f.now - f.lastTryAt < BRD_UPPER_CITY_RETRY_SECONDS)
        return BrdWingStep::WaitChain;
    if (f.tries >= BRD_UPPER_CITY_TRIES)
        return BrdWingStep::Nothing;  // never a 'complete' for a chain that did not take
    return BrdWingStep::ChainUpperCity;
}

inline char const* BrdWingStepWord(BrdWingStep step)
{
    switch (step)
    {
        case BrdWingStep::Nothing:              return "nothing";
        case BrdWingStep::ChainUpperCity:       return "chain to the Upper City";
        case BrdWingStep::WaitChain:            return "wait for the chain";
        case BrdWingStep::FinishDetentionBlock: return "finish the Detention Block";
    }
    return "?";
}

}  // namespace OverseerDecisions

#endif  // OVERSEER_BRD_WING_H
