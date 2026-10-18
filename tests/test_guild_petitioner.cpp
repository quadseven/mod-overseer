/*
 * The `petitioner` travel keyword can resolve to an Arena Organizer (#544).
 *
 * UNIT_NPC_FLAG_PETITIONER (0x40000) is carried by guild petitioners and arena
 * team petitioners alike; the pinned core's own comment on the constant reads
 * "0xC0000 = guild petitions, 0x40000 = arena team petitions". Measured on a
 * dev realm: `npcflag & 0x40000` with the tabard bit clear matched six NPCs,
 * all Arena Organizers, and `npcflag & 0xC0000 = 0xC0000` matched nine, all
 * Guild Masters.
 *
 * So a role that wants a guild petitioner has to ask for both bits, and the
 * spawn match has to require every bit the role names rather than any one of
 * them. With the old any-bit test, a two-bit role would still have been filled
 * by an Arena Organizer, which is the whole defect over again.
 *
 * The flag values in the cases below are built from the constants, with the
 * gossip bit (0x1) the way real spawns usually carry it alongside; they are not
 * quoted rows.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

using OverseerDecisions::NPC_FLAG_AUCTIONEER;
using OverseerDecisions::NPC_FLAG_BANKER;
using OverseerDecisions::NPC_FLAG_FLIGHTMASTER;
using OverseerDecisions::NPC_FLAG_GUILD_PETITIONER;
using OverseerDecisions::NPC_FLAG_PETITIONER;
using OverseerDecisions::NPC_FLAG_REPAIR;
using OverseerDecisions::NPC_FLAG_TABARDDESIGNER;
using OverseerDecisions::NPC_FLAG_TRAINER;
using OverseerDecisions::NPC_FLAG_VENDOR;
using OverseerDecisions::SpawnFillsTravelRole;

namespace
{

int failures = 0;

constexpr std::uint32_t GOSSIP = 0x00000001;
constexpr std::uint32_t ARENA_ORGANIZER = GOSSIP | NPC_FLAG_PETITIONER;
constexpr std::uint32_t GUILD_MASTER =
    GOSSIP | NPC_FLAG_PETITIONER | NPC_FLAG_TABARDDESIGNER;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

// The arithmetic, against the core's own comment. A mistyped constant here
// would route a guild errand at the wrong sort of creature.
void TheGuildPetitionerIsBothBits()
{
    Check("PETITIONER is 0x40000", NPC_FLAG_PETITIONER == 0x00040000u, true);
    Check("TABARDDESIGNER is 0x80000", NPC_FLAG_TABARDDESIGNER == 0x00080000u, true);
    Check("GUILD_PETITIONER is 0xC0000", NPC_FLAG_GUILD_PETITIONER == 0x000C0000u, true);
}

// THE DEFECT. An Arena Organizer carries the petitioner bit and not the tabard
// bit, and must not fill a guild petitioner role.
void AnArenaOrganizerIsNotAGuildPetitioner()
{
    Check("arena organizer as guild petitioner",
          SpawnFillsTravelRole(ARENA_ORGANIZER, NPC_FLAG_GUILD_PETITIONER), false);
    Check("bare petitioner bit as guild petitioner",
          SpawnFillsTravelRole(NPC_FLAG_PETITIONER, NPC_FLAG_GUILD_PETITIONER), false);
}

void AGuildMasterIsAGuildPetitioner()
{
    Check("guild master as guild petitioner",
          SpawnFillsTravelRole(GUILD_MASTER, NPC_FLAG_GUILD_PETITIONER), true);
    Check("exactly 0xC0000 as guild petitioner",
          SpawnFillsTravelRole(NPC_FLAG_GUILD_PETITIONER, NPC_FLAG_GUILD_PETITIONER), true);
}

// A tabard designer that takes no petitions is not one either: both bits.
void ATabardDesignerAloneIsNotAGuildPetitioner()
{
    Check("tabard designer alone as guild petitioner",
          SpawnFillsTravelRole(GOSSIP | NPC_FLAG_TABARDDESIGNER, NPC_FLAG_GUILD_PETITIONER),
          false);
}

// THE EXISTING `petitioner` KEYWORD IS UNCHANGED. It asks for the one bit, and
// both kinds of petitioner carry it, so it still finds either. Whatever relied
// on it keeps working; a guild errand should name `guild petitioner`.
void PetitionerStillMeansAnyPetitioner()
{
    Check("arena organizer as petitioner",
          SpawnFillsTravelRole(ARENA_ORGANIZER, NPC_FLAG_PETITIONER), true);
    Check("guild master as petitioner",
          SpawnFillsTravelRole(GUILD_MASTER, NPC_FLAG_PETITIONER), true);
    Check("guild master as tabard designer",
          SpawnFillsTravelRole(GUILD_MASTER, NPC_FLAG_TABARDDESIGNER), true);
    Check("arena organizer as tabard designer",
          SpawnFillsTravelRole(ARENA_ORGANIZER, NPC_FLAG_TABARDDESIGNER), false);
}

// FOR EVERY SINGLE-BIT ROLE, ALL-BITS IS ANY-BIT. That is what makes the new
// rule safe for the twelve roles that were already right: asserted over every
// bit position and a spread of spawn flag values, rather than promised.
void SingleBitRolesBehaveExactlyAsBefore()
{
    std::uint32_t const spawns[] = {
        0u,
        GOSSIP,
        NPC_FLAG_VENDOR | NPC_FLAG_REPAIR,           // a repair vendor, 4224
        NPC_FLAG_FLIGHTMASTER | 0x2u | GOSSIP,       // a flight master, 8195
        NPC_FLAG_TRAINER | GOSSIP,
        NPC_FLAG_BANKER,
        NPC_FLAG_AUCTIONEER | GOSSIP,
        ARENA_ORGANIZER,
        GUILD_MASTER,
        0xFFFFFFFFu,
    };
    for (unsigned bit = 0; bit < 32; ++bit)
    {
        std::uint32_t const role = 1u << bit;
        for (std::uint32_t spawn : spawns)
        {
            bool const before = (spawn & role) != 0;
            if (SpawnFillsTravelRole(spawn, role) != before)
            {
                std::printf("FAIL single-bit role 0x%x on spawn 0x%x changed meaning\n",
                            role, spawn);
                ++failures;
            }
        }
    }
}

// No bits is no role. "Every one of no bits" is vacuously true and would match
// every spawn on the map.
void ARoleOfNoBitsIsFilledByNothing()
{
    Check("no role on a guild master", SpawnFillsTravelRole(GUILD_MASTER, 0u), false);
    Check("no role on no flags", SpawnFillsTravelRole(0u, 0u), false);
}

}  // namespace

int main()
{
    TheGuildPetitionerIsBothBits();
    AnArenaOrganizerIsNotAGuildPetitioner();
    AGuildMasterIsAGuildPetitioner();
    ATabardDesignerAloneIsNotAGuildPetitioner();
    PetitionerStillMeansAnyPetitioner();
    SingleBitRolesBehaveExactlyAsBefore();
    ARoleOfNoBitsIsFilledByNothing();

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("a guild errand is walked to a Guild Master, never an Arena Organizer\n");
    return EXIT_SUCCESS;
}
