/*
 * A picked lockbox is opened and leaves the bags (#531).
 *
 * Before this, an `open items` row handed the text to mod-playerbots'
 * OpenItemAction and came back `delivered`: the picked boxes stayed in the
 * rogue's bags, because the bot's loot strategy closes a window untaken on
 * near-full bags and FindOpenableItem reads the lock off the FIRST box of an
 * entry. The decisions below choose boxes per instance and say what a pass
 * did; the source contract pins that DoOpenLockboxes opens the boxes itself
 * and reads back whether each one left the bags.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using OverseerDecisions::LockboxCarried;
using OverseerDecisions::LockboxesToOpen;
using OverseerDecisions::LockboxOpenOutcome;
using OverseerDecisions::LockboxOpenTally;

namespace
{
int failures = 0;

void Check(char const* what, bool value)
{
    if (!value)
    {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

LockboxCarried Box(bool locked)
{
    LockboxCarried box;
    box.lockbox = true;
    box.hasLoot = true;
    box.locked = locked;
    return box;
}

// The text of the member function that starts at `signature`, up to the next
// line that closes a member function at the class's indentation.
std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}

void Decisions()
{
    // Three boxes of one entry, the first still locked: upstream's
    // GetItemByEntry test hid the two picked ones behind it.
    std::vector<LockboxCarried> const sameEntry{Box(true), Box(false), Box(false)};
    std::vector<std::size_t> const open = LockboxesToOpen(sameEntry);
    Check("a picked box behind a locked box of its entry is opened",
          open.size() == 2 && open[0] == 1 && open[1] == 2);

    LockboxCarried plain;  // a sword: no lock, no loot
    LockboxCarried clam;   // loot, but no lock: not this verb's to open
    clam.hasLoot = true;
    LockboxCarried wrapped = Box(false);
    wrapped.wrapped = true;
    Check("only unlocked lockboxes with loot are opened",
          LockboxesToOpen({plain, clam, wrapped, Box(true)}).empty());

    LockboxOpenTally none;
    Check("no picked box yet is delivered: only the unlock was queued",
          LockboxOpenOutcome(none).status == "delivered" &&
              LockboxOpenOutcome(none).outcome == "no_unlocked_box");

    LockboxOpenTally emptied;
    emptied.openable = 3;
    emptied.opened = 3;
    emptied.consumed = 3;
    Check("boxes that left the bags are delivered as consumed",
          LockboxOpenOutcome(emptied).status == "delivered" &&
              LockboxOpenOutcome(emptied).outcome == "consumed");

    LockboxOpenTally full;
    full.openable = 2;
    full.opened = 2;
    full.kept = 2;
    Check("opened boxes still carried are not reported as delivered",
          LockboxOpenOutcome(full).status == "unchanged" &&
              LockboxOpenOutcome(full).outcome == "kept");

    LockboxOpenTally partly = full;
    partly.consumed = 1;
    partly.kept = 1;
    Check("a pass that emptied some boxes says some were kept",
          LockboxOpenOutcome(partly).outcome == "partly_consumed");

    LockboxOpenTally busy;
    busy.openable = 1;
    busy.windowBusy = true;
    Check("an open loot window holds the boxes a pass, said as unchanged",
          LockboxOpenOutcome(busy).status == "unchanged" &&
              LockboxOpenOutcome(busy).outcome == "loot_window_busy");

    LockboxOpenTally refused;
    refused.openable = 1;
    Check("an unlocked box the core would not open is an error",
          LockboxOpenOutcome(refused).status == "error");
}

void SourceContract()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
    {
        Check("cannot read src/mod_overseer.cpp", false);
        return;
    }
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    std::string const body = Body(text, "static char const* DoOpenLockboxes(");
    if (body.empty())
    {
        Check("cannot find DoOpenLockboxes", false);
        return;
    }

    Check("the open half is no longer handed to the bot's `open items`",
          body.find("\"open items\", player)") == std::string::npos);
    Check("the picked boxes are chosen per instance",
          body.find("OverseerDecisions::LockboxesToOpen(") != std::string::npos);
    Check("each box is opened with the client's CMSG_OPEN_ITEM",
          body.find("HandleOpenItemOpcode(") != std::string::npos);
    Check("its loot is taken before the window is released",
          body.find("HandleAutostoreLootItemOpcode(") != std::string::npos &&
              body.find("HandleAutostoreLootItemOpcode(") <
                  body.find("HandleLootReleaseOpcode("));
    std::size_t const release = body.find("HandleLootReleaseOpcode(");
    Check("whether the box left the bags is read back after the release",
          release != std::string::npos &&
              body.find("GetItemByGuid(boxGuid)", release) != std::string::npos);
    Check("the row's status comes from the read-back verdict",
          body.find("OverseerDecisions::LockboxOpenOutcome(") != std::string::npos);
}
}  // namespace

int main()
{
    Decisions();
    SourceContract();
    return failures;
}
