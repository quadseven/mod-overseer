"""Replay StoreLootAction's bag-space shortcut, pinned and patched (#1424).

The fixture is src/Ai/Base/Actions/LootAction.cpp exactly as the realm pin
ships it. The harness takes the real lines of StoreLootAction::Execute from
the item template read through the end of the "bag space" > 80 block, and
compiles them twice, once as pinned and once with this repository's patch
applied, against stand-ins for the item template, the bot's master and the
"bag space" value. A `continue` out of those lines is a skipped item.

Pinned, a bot at 86 percent bag space (a family member, 2026-11-08) leaves
the Prison Cell Key (11140) and Ironfel (10999) on the corpse. Patched, it
takes both and still skips an ordinary item that does not stack.

No realm, database or network is used.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE.parent / "patches/mod-playerbots/0023-a-key-or-quest-item-is-not-left-for-a-full-bag.patch"
FIXTURE = HERE / "fixtures/loot_key/LootAction.cpp"
TARGET = Path("src/Ai/Base/Actions/LootAction.cpp")


def block_end(source, start):
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return end


def shortcut(source):
    """The real lines: the template read through the bag-space block."""
    execute = source.index("bool StoreLootAction::Execute(Event event)")
    start = source.index("ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemid);", execute)
    test = source.index('AI_VALUE(uint8, "bag space") > 80', start)
    return source[start:block_end(source, test)]


HARNESS = r"""
#include <cstdint>
#include <cstdio>
#include <vector>

using uint32 = uint32_t;
using uint8 = uint8_t;

enum { BAG_FAMILY_MASK_KEYS = 0x00000100 };
enum { ITEM_CLASS_ARMOR = 4, ITEM_CLASS_QUEST = 12, ITEM_CLASS_KEY = 13 };
enum { BIND_WHEN_PICKED_UP = 1, BIND_QUEST_ITEM = 4 };

struct ItemTemplate
{
    uint32 Class;
    uint32 BagFamily;
    uint32 Bonding;
    uint32 MaxStack;
    uint32 GetMaxStackSize() const { return MaxStack; }
};
struct Item { uint32 GetCount() const { return 1; } };
struct Player {};
// Every family member's master carries a PlayerbotAI: not a real player.
bool IsRealPlayer(Player*) { return false; }
struct Ai { Player* GetMaster() { return nullptr; } };
struct Chat { int FormatItem(ItemTemplate const*) { return 0; } };
struct Templates
{
    ItemTemplate const* item;
    ItemTemplate const* GetItemTemplate(uint32) { return item; }
};

static int bagSpace = 0;
#define AI_VALUE(type, name) bagSpace

struct Case
{
    char const* name;
    ItemTemplate item;
    bool takenPinned;   // what the pinned code does at 86 percent
    bool takenPatched;  // what the patched code must do at 86 percent
};

// True when the real lines `continue` past the item: it is left behind.
static bool Skipped(ItemTemplate const& item, int bag)
{
    Ai ai;
    Ai* botAI = &ai;
    Chat chatter;
    Chat* chat = &chatter;
    Templates templates{&item};
    Templates* sObjectMgr = &templates;
    auto parseItems = [](int) { return std::vector<Item*>(); };
    uint32 itemid = 1;
    uint32 itemcount = 1;
    bagSpace = bag;
    (void)botAI; (void)chat; (void)parseItems; (void)itemcount;
    for (int once = 0; once < 1; ++once)
    {
/*SHORTCUT*/
        return false;
    }
    return true;
}

int main()
{
    Case const cases[] = {
        {"Prison Cell Key (11140)", {ITEM_CLASS_KEY, BAG_FAMILY_MASK_KEYS, BIND_WHEN_PICKED_UP, 1}, false, true},
        {"Ironfel (10999)", {ITEM_CLASS_QUEST, 0, BIND_QUEST_ITEM, 1}, false, true},
        {"an ordinary piece of armor", {ITEM_CLASS_ARMOR, 0, BIND_WHEN_PICKED_UP, 1}, false, false},
    };
    int wrong = 0;
    for (Case const& c : cases)
    {
        bool const taken = !Skipped(c.item, 86);
        std::printf("%s at 86%%: %s\n", c.name, taken ? "taken" : "left on the corpse");
        // Checked in both builds: the pinned one must reproduce the defect,
        // the patched one must not.
        if (taken != (EXPECT_PATCHED ? c.takenPatched : c.takenPinned))
            ++wrong;
        // Below the shortcut nothing changes either way.
        if (Skipped(c.item, 50))
            ++wrong;
    }
    return wrong;
}
"""


class LootKeyBagSpaceReplayTest(unittest.TestCase):
    def build_and_run(self, source, patched):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        self.assertIsNotNone(compiler, "C++17 compiler required")
        lines = shortcut(source)
        self.assertIn("continue;", lines)
        program = HARNESS.replace("/*SHORTCUT*/", lines).replace(
            "EXPECT_PATCHED", "true" if patched else "false"
        )
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            cpp, binary = folder / "replay.cpp", folder / "replay"
            cpp.write_text(program)
            built = subprocess.run(  # noqa: S603 - fixed repository fixture
                [compiler, "-std=c++17", str(cpp), "-o", str(binary)],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(0, built.returncode, built.stdout + built.stderr)
            return subprocess.run(  # noqa: S603 - compiled fixture
                [str(binary)], capture_output=True, text=True, timeout=10
            )

    def patched_source(self):
        with tempfile.TemporaryDirectory() as temp:
            tree = Path(temp)
            (tree / TARGET).parent.mkdir(parents=True)
            shutil.copy(FIXTURE, tree / TARGET)
            subprocess.run(["git", "init", "-q", str(tree)], check=True)  # noqa: S603,S607
            applied = subprocess.run(  # noqa: S603,S607
                ["git", "-C", str(tree), "apply", str(PATCH)],
                capture_output=True, text=True,
            )
            self.assertEqual(0, applied.returncode, applied.stderr)
            return (tree / TARGET).read_text()

    def test_pinned_leaves_the_key_and_ironfel(self):
        run = self.build_and_run(FIXTURE.read_text(), patched=False)
        self.assertEqual(0, run.returncode, run.stdout + run.stderr)
        self.assertIn("Prison Cell Key (11140) at 86%: left on the corpse", run.stdout)
        self.assertIn("Ironfel (10999) at 86%: left on the corpse", run.stdout)

    def test_patched_takes_the_key_and_ironfel_and_still_skips_the_rest(self):
        run = self.build_and_run(self.patched_source(), patched=True)
        self.assertEqual(0, run.returncode, run.stdout + run.stderr)
        self.assertIn("Prison Cell Key (11140) at 86%: taken", run.stdout)
        self.assertIn("Ironfel (10999) at 86%: taken", run.stdout)
        self.assertIn("an ordinary piece of armor at 86%: left on the corpse", run.stdout)


if __name__ == "__main__":
    unittest.main()
