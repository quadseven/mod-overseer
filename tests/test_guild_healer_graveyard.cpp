/*
 * A guild ghost does not take the spirit healer at the graveyard that kills it.
 *
 * On the dev realm seven Horde members, levels 23 to 25, died 318 times in six
 * hours at one Hillsbrad graveyard to Alliance footmen (level 26). Each repeat
 * death took the nearest spirit healer although the graveyard had been judged
 * "5 hostile spawn(s) above level 25", and the member was revived into the
 * footmen again. These pin the choice of another graveyard.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::GhostHealerWalkSeconds;
using OverseerDecisions::GuildHealerGraveyard;
using OverseerDecisions::HealerServesGraveyard;
using OverseerDecisions::PickGuildHealerGraveyard;

namespace
{

int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main()
{
    Check(PickGuildHealerGraveyard(true, true) == GuildHealerGraveyard::Nearest,
          "a safe nearest graveyard is kept even when another exists");
    Check(PickGuildHealerGraveyard(true, false) == GuildHealerGraveyard::Nearest,
          "a safe nearest graveyard is kept");
    Check(PickGuildHealerGraveyard(false, true) == GuildHealerGraveyard::Alternative,
          "an unsafe nearest graveyard gives way to a safe one");
    Check(PickGuildHealerGraveyard(false, false) == GuildHealerGraveyard::Nearest,
          "with no safe graveyard anywhere the nearest stands");

    Check(HealerServesGraveyard(10.f, 60.f), "a healer at the graveyard serves it");
    Check(HealerServesGraveyard(60.f, 60.f), "a healer at the sweep edge serves it");
    Check(!HealerServesGraveyard(900.f, 60.f),
          "the healer the ghost stands beside at the unsafe graveyard does not serve the chosen one");
    Check(!HealerServesGraveyard(-1.f, 60.f), "an unmeasured distance serves nothing");

    Check(GhostHealerWalkSeconds(0.f, 120) == 120, "no distance keeps the base allowance");
    Check(GhostHealerWalkSeconds(300.f, 120) == 120, "a short walk keeps the base allowance");
    Check(GhostHealerWalkSeconds(1400.f, 120) > 120, "a long walk gets longer than the base");
    Check(GhostHealerWalkSeconds(1400.f, 120) >= 1400 / 7, "and at least the walk itself");

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
