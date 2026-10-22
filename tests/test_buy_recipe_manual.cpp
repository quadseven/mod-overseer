/*
 * A roster character walks to a vendor, buys the recipe manual the row names,
 * and uses it from the bag - the vendor-taught half of a trade that TrainOnArrival
 * never reaches (Heavy Silk Bandage, Smoked Bear Meat).
 *
 * DecideVendorManualPurchase is the pure decision: given the manual, the vendor's
 * stock (item entry -> copper price) and the purse, say whether the purchase can
 * be served, and name why when it cannot. Each check below is one row of its
 * table. The last part reads src/mod_overseer.cpp (run from the repo root) to
 * pin that the drive exists and is wired to the decision.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else.
 */

#include "overseer_decisions.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using OverseerDecisions::DecideVendorManualPurchase;
using OverseerDecisions::ParseVendorManual;
using OverseerDecisions::VendorManual;
using OverseerDecisions::VendorManualVerdict;
using OverseerDecisions::VendorPrice;
using OverseerDecisions::VendorStock;

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

VendorManual Manual(uint32_t item, uint32_t vendor)
{
    VendorManual m;
    m.itemEntry = item;
    m.vendorEntry = vendor;
    return m;
}

VendorStock Stock(std::initializer_list<std::pair<uint32_t const, uint64_t>> list)
{
    VendorStock out;
    for (auto const& kv : list)
        out.emplace(kv);
    return out;
}

std::string Source()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

int main()
{
    // Row 1: the column parses "<itemEntry>:<vendorEntry>".
    {
        VendorManual m;
        Check("parses a well-formed column",
              ParseVendorManual("3276:1234", m) && m.itemEntry == 3276 && m.vendorEntry == 1234);
    }
    // Row 2: malformed columns never parse.
    {
        VendorManual m{9, 9};
        Check("rejects an empty column", !ParseVendorManual("", m) && m.itemEntry == 0 && m.vendorEntry == 0);
        Check("rejects a bare colon", !ParseVendorManual(":", m));
        Check("rejects no vendor side", !ParseVendorManual("123:", m));
        Check("rejects no item side", !ParseVendorManual(":123", m));
        Check("rejects a zero item", !ParseVendorManual("0:123", m));
        Check("rejects a zero vendor", !ParseVendorManual("123:0", m));
        Check("rejects non-numeric text", !ParseVendorManual("abc:123", m));
        Check("rejects a trailing extra field", !ParseVendorManual("123:456:789", m));
    }
    // Row 3: a vendor that stocks the manual and a purse that covers it -> Buy.
    {
        VendorManualVerdict const v =
            DecideVendorManualPurchase(Manual(3276, 1234), Stock({{3276, 100}}), 1000);
        Check("buys when stocked and affordable",
              v.kind == VendorManualVerdict::Kind::Buy && v.price == 100 && v.reason.empty());
    }
    // Row 4: the vendor stocks it but the purse is short -> CannotAfford, naming why.
    {
        VendorManualVerdict const v =
            DecideVendorManualPurchase(Manual(3276, 1234), Stock({{3276, 100}}), 50);
        Check("refuses when the purse is short",
              v.kind == VendorManualVerdict::Kind::CannotAfford && v.price == 100);
        Check("names the item and the price in the refusal",
              v.reason.find("cannot afford item 3276 at 100 copper") != std::string::npos &&
                  v.reason.find("holds 50") != std::string::npos);
    }
    // Row 5: exactly the price is affordable (floor does not apply to a vendor buy).
    {
        VendorManualVerdict const v =
            DecideVendorManualPurchase(Manual(3276, 1234), Stock({{3276, 100}}), 100);
        Check("buys down to the last copper",
              v.kind == VendorManualVerdict::Kind::Buy && v.price == 100 && v.reason.empty());
    }
    // Row 6: no vendor on the map stocks the manual -> NotSold, naming why.
    {
        VendorManualVerdict const v =
            DecideVendorManualPurchase(Manual(9999, 1234), Stock({{3276, 100}}), 100000);
        Check("refuses when nothing stocks it", v.kind == VendorManualVerdict::Kind::NotSold);
        Check("names the missing item in the refusal",
              v.reason.find("no vendor on this map sells item 9999") != std::string::npos);
    }
    // Row 7: an empty stock never sells anything.
    {
        VendorManualVerdict const v =
            DecideVendorManualPurchase(Manual(3276, 1234), VendorStock(), 100000);
        Check("an empty stock is not sold", v.kind == VendorManualVerdict::Kind::NotSold);
    }

    // The drive exists in the module source and is wired to this decision.
    {
        std::string const src = Source();
        Check("adapter source readable", !src.empty());
        Check("drive reads the buy_recipe_item column",
              src.find("buy_recipe_item") != std::string::npos);
        Check("drive parses the manual from the column",
              src.find("OverseerDecisions::ParseVendorManual(") != std::string::npos);
        Check("drive asks the purchase decision",
              src.find("OverseerDecisions::DecideVendorManualPurchase(") != std::string::npos);
        Check("drive clears the column on a refusal, releasing the errand",
              src.find("ClearBuyRecipeAim(") != std::string::npos);
    }

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
