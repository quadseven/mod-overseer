/*
 * The sale and the purchase both name the nearest vendor in their
 * "vendor not in range" refusal row (wow-overseer#2220).
 *
 * Reads src/mod_overseer.cpp as text: the wiring lives in the adapter, which
 * this binary cannot compile. What the row says is decided by
 * OverseerDecisions::KeepNearestVendor and NearestVendorJson, pinned in
 * tests/test_nearest_vendor.cpp; this pins that both executors ask them.
 */

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

std::string Body(std::string const& src, char const* signature, char const* close)
{
    std::size_t const at = src.find(signature);
    if (at == std::string::npos)
        return std::string();
    std::size_t const end = src.find(close, at);
    return src.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

bool Has(std::string const& hay, char const* needle)
{
    return hay.find(needle) != std::string::npos;
}

std::size_t At(std::string const& hay, char const* needle)
{
    return hay.find(needle);
}

} // namespace

int main()
{
    std::ifstream in("src/mod_overseer.cpp");
    if (!in)
    {
        std::printf("FAIL could not read src/mod_overseer.cpp (run from the repo root)\n");
        return 1;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string const src = buf.str();

    // ---- the sale -----------------------------------------------------------

    std::string const sell = Body(src, "    static char const* DoSell(", "\n    }\n\n");
    Check("DoSell found", !sell.empty());

    std::size_t const sellKeep = At(
        sell, "OverseerDecisions::KeepNearestVendor(nearest, yards, creature->GetEntry(),");
    std::size_t const sellGate =
        At(sell, "!seller->GetNPCIfCanInteractWith(creature->GetGUID(), UNIT_NPC_FLAG_VENDOR)");
    Check("the sale's sweep keeps the nearest vendor with its entry",
          sellKeep != std::string::npos);
    Check("and keeps it before the interact gate, so one out of reach still counts",
          sellKeep != std::string::npos && sellGate != std::string::npos && sellKeep < sellGate);

    std::size_t const sellEmpty = At(sell, "if (reachable.empty())");
    std::size_t const sellRefuse = At(sell, "return refuse(\"vendor not in range\",");
    Check("the sale still refuses when nobody is in reach",
          sellEmpty != std::string::npos && sellRefuse != std::string::npos &&
              sellEmpty < sellRefuse);
    std::string const sellRefusal =
        sellEmpty == std::string::npos || sellRefuse == std::string::npos
            ? std::string()
            : sell.substr(sellEmpty, sellRefuse - sellEmpty);
    Check("and that refusal row names the nearest vendor",
          Has(sellRefusal, "OverseerDecisions::NearestVendorJson(nearest)"));
    Check("the sale no longer writes the bare distance by hand",
          !Has(sell, "\\\"nearest_vendor_yards\\\""));

    // ---- the purchase -------------------------------------------------------

    std::string const buy = Body(src, "    static char const* DoBuy(", "\n    }\n\n");
    Check("DoBuy found", !buy.empty());

    std::size_t const buyKeep = At(
        buy, "OverseerDecisions::KeepNearestVendor(ev.nearest, yards, creature->GetEntry(),");
    std::size_t const buyGate =
        At(buy, "!buyer->GetNPCIfCanInteractWith(creature->GetGUID(), UNIT_NPC_FLAG_VENDOR)");
    Check("the purchase's sweep keeps the nearest vendor with its entry",
          buyKeep != std::string::npos);
    Check("and keeps it before the interact gate",
          buyKeep != std::string::npos && buyGate != std::string::npos && buyKeep < buyGate);

    std::string const describe = Body(buy, "auto describe = [&]", "\n        };\n");
    Check("the purchase's row writer found", !describe.empty());
    Check("a purchase row with no vendor in reach names the nearest vendor",
          Has(describe, "if (!ev.haveVendor)\n"
                        "                o << OverseerDecisions::NearestVendorJson(ev.nearest);"));
    Check("the purchase still refuses when nobody is in reach",
          Has(buy, "if (reachable.empty())\n            return refuse(\"vendor not in range\");"));

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok - both vendor refusals name the vendor they stood next to\n");
    return 0;
}
