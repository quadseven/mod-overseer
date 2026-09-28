// Spark-authored: qwen3-coder-next:q4_K_M on an on-prem Spark, 2026-09-28; review pending
/*
 * Junk to sell before a weapon is bought (#787).
 *
 * A guild member with full bags could not buy a weapon ("no room in its
 * bags"). It sells grey items to the vendor first, lowest price first, and
 * never a protected one. Compiled against src/overseer_decisions.cpp only.
 */

#include "overseer_decisions.h"

#include <cstdio>

using OverseerDecisions::ChooseJunkToSell;
using OverseerDecisions::JunkItem;

namespace
{

int failures = 0;

void Check(char const* what, std::vector<size_t> const& got, std::vector<size_t> const& want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %zu indexes, wanted %zu\n", what, got.size(), want.size());
    ++failures;
}

JunkItem Item(uint32_t price, uint32_t quality, bool prot = false)
{
    JunkItem i;
    i.itemEntry = 1;
    i.sellPrice = price;
    i.quality = quality;
    i.isProtected = prot;
    return i;
}

}  // namespace

int main()
{
    Check("no bags, nothing sold", ChooseJunkToSell({}, 3), {});
    Check("no room needed, nothing sold", ChooseJunkToSell({Item(5, 0)}, 0), {});

    Check("lowest price goes first, result in bag order",
          ChooseJunkToSell({Item(50, 0), Item(10, 0), Item(30, 0)}, 2), {1, 2});
    Check("a tie on price goes to the lower index",
          ChooseJunkToSell({Item(10, 0), Item(10, 0)}, 1), {0});
    Check("fewer greys than slots wanted sells them all",
          ChooseJunkToSell({Item(7, 0), Item(3, 0)}, 5), {0, 1});
    Check("only grey is junk: white and green stay",
          ChooseJunkToSell({Item(1, 1), Item(2, 2), Item(9, 0)}, 3), {2});
    Check("a protected grey (quest item) is never sold",
          ChooseJunkToSell({Item(1, 0, true), Item(9, 0)}, 3), {1});
    Check("all protected, nothing sold", ChooseJunkToSell({Item(1, 0, true)}, 1), {});

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("junk sell: all checks passed\n");
    return 0;
}
