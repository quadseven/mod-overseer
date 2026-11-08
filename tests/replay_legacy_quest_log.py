"""Replay NewRpgBaseAction::OrganizeQuestLog, pinned and patched (#1444).

The fixture is src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp exactly as the realm
pin ships it. The harness takes the real text of OrganizeQuestLog (and, once
patched, the helper that precedes it) and compiles it twice, once as pinned and
once with this repository's patch applied, against stand-ins for the bot, its
quest log, the object manager and the removal packet.

Pinned, a bot whose 25-slot log is full removes Dark Iron Legacy (3802: a
dungeon-type quest of the Blackrock Depths zone, 2026-11-08) along with the
other quests it judges not capable. Patched, 3802 and 3801 keep their slots and
every other quest is removed exactly as before.

No realm, database or network is used.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = HERE.parent / "patches/mod-playerbots/0024-the-legacy-quests-keep-their-log-slot.patch"
FIXTURE = HERE / "fixtures/legacy_quest_log/NewRpgBaseAction.cpp"
TARGET = Path("src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp")


def function_text(source):
    """OrganizeQuestLog as written, with whatever static helper sits above it."""
    head = source.index("bool NewRpgBaseAction::OrganizeQuestLog()")
    end = source.index("bool NewRpgBaseAction::SearchQuestGiverAndAcceptOrReward()")
    # Helpers the patch adds sit between IsQuestCapableDoing and the function.
    after_capable = source.index("bool NewRpgBaseAction::IsQuestCapableDoing(Quest const* quest)")
    helper_from = source.index("\n}\n", after_capable) + 3
    helper = source[helper_from:head]
    body = source[head:end].replace("NewRpgBaseAction::OrganizeQuestLog", "Harness::OrganizeQuestLog")
    return helper + body


HARNESS = r"""
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using int32 = int32_t;
using int64 = int64_t;

constexpr uint16 MAX_QUEST_LOG_SIZE = 25;
enum QuestStatus { QUEST_STATUS_NONE = 0, QUEST_STATUS_COMPLETE = 1, QUEST_STATUS_INCOMPLETE = 3, QUEST_STATUS_FAILED = 5 };
enum { CMSG_QUESTLOG_REMOVE_QUEST = 1 };

#define LOG_DEBUG(...) ((void)0)

struct Quest
{
    int zoneOrSort;
    int type;
    bool worth;
    int32 GetZoneOrSort() const { return zoneOrSort; }
};

struct Templates
{
    std::map<uint32, Quest> quests;
    Quest const* GetQuestTemplate(uint32 id) const { return &quests.at(id); }
};
Templates templates;
Templates* sObjectMgr = &templates;

struct WorldPacket
{
    uint8 slot = 0;
    explicit WorldPacket(int) {}
};
WorldPacket& operator<<(WorldPacket& p, uint8 slot) { p.slot = slot; return p; }
namespace WorldPackets { namespace Quest {
struct QuestLogRemoveQuest
{
    WorldPacket packet;
    explicit QuestLogRemoveQuest(WorldPacket&& p) : packet(p) {}
    void Read() {}
};
} }

struct Session;
struct Bot
{
    uint32 log[MAX_QUEST_LOG_SIZE] = {};
    std::vector<uint32> removed;
    Session* session = nullptr;
    uint32 GetQuestSlotQuestId(uint16 slot) const { return log[slot]; }
    QuestStatus GetQuestStatus(uint32) const { return QUEST_STATUS_INCOMPLETE; }
    int GetZoneId() const { return 25; }  // Blackrock Mountain, not zone 1584
    char const* GetName() const { return "Zug"; }
    Session* GetSession() const { return session; }
};
struct Session
{
    Bot* bot;
    void HandleQuestLogRemoveQuest(WorldPackets::Quest::QuestLogRemoveQuest& r)
    {
        uint8 const slot = r.packet.slot;
        bot->removed.push_back(bot->log[slot]);
        bot->log[slot] = 0;
    }
};

struct Master {};
struct BotAi
{
    struct { int questDropped = 0; } rpgStatistic;
    Master* GetMaster() { return nullptr; }
    void TellMasterNoFacing(std::string const&) {}
};
struct TextMgr
{
    static TextMgr& instance() { static TextMgr t; return t; }
    std::string GetBotTextOrDefault(char const*, char const*, std::map<std::string, std::string> const&) { return {}; }
};
using PlayerbotTextMgr = TextMgr;
struct ChatHelper { static std::string FormatQuest(Quest const*) { return {}; } };

struct Harness
{
    Bot* bot;
    BotAi* botAI;
    bool IsQuestWorthDoing(Quest const* q) { return q->worth; }
    bool IsQuestCapableDoing(Quest const* q) { return q->type == 0; }
    bool OrganizeQuestLog();
};

/*FUNCTION*/

// Dungeon-type (81) quest of zone 1584, as Dark Iron Legacy is.
static Quest dungeon() { return Quest{1584, 81, true}; }
// An ordinary quest of another zone the bot would also tidy away.
static Quest elsewhere() { return Quest{17, 0, true}; }

int main()
{
    int wrong = 0;
    // A log of 25: slot 0 holds 3802, slot 1 holds 3801 (not yet handed in),
    // slot 2 an ordinary dungeon-type quest (7001), the rest quests of other zones.
    templates.quests[3802] = dungeon();
    templates.quests[3801] = dungeon();
    templates.quests[7001] = dungeon();
    for (uint32 id = 8000; id < 8022; ++id)
        templates.quests[id] = elsewhere();

    Bot bot;
    Session session{&bot};
    bot.session = &session;
    BotAi ai;
    bot.log[0] = 3802;
    bot.log[1] = 3801;
    bot.log[2] = 7001;
    for (uint16 s = 3; s < MAX_QUEST_LOG_SIZE; ++s)
        bot.log[s] = 8000 + (s - 3);

    Harness h{&bot, &ai};
    h.OrganizeQuestLog();

    bool legacyKept = bot.log[0] == 3802 && bot.log[1] == 3801;
    bool ordinaryRemoved = bot.log[2] == 0;
    std::printf("3802 and 3801 %s\n", legacyKept ? "kept" : "removed");
    std::printf("an ordinary dungeon-type quest %s\n", ordinaryRemoved ? "removed" : "kept");
    std::printf("quests removed in all: %zu\n", bot.removed.size());
    if (legacyKept != EXPECT_KEPT)
        ++wrong;
    if (!ordinaryRemoved)
        ++wrong;
    return wrong;
}
"""


class LegacyQuestLogReplayTest(unittest.TestCase):
    def build_and_run(self, source, kept):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        self.assertIsNotNone(compiler, "C++17 compiler required")
        program = HARNESS.replace("/*FUNCTION*/", function_text(source)).replace(
            "EXPECT_KEPT", "true" if kept else "false"
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

    def test_pinned_removes_the_legacy_quests_from_a_full_log(self):
        run = self.build_and_run(FIXTURE.read_text(), kept=False)
        self.assertEqual(0, run.returncode, run.stdout + run.stderr)
        self.assertIn("3802 and 3801 removed", run.stdout)

    def test_patched_keeps_the_legacy_quests_and_still_tidies_the_rest(self):
        run = self.build_and_run(self.patched_source(), kept=True)
        self.assertEqual(0, run.returncode, run.stdout + run.stderr)
        self.assertIn("3802 and 3801 kept", run.stdout)
        self.assertIn("an ordinary dungeon-type quest removed", run.stdout)


if __name__ == "__main__":
    unittest.main()
