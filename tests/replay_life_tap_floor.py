"""Execute the real LifeTapTrigger::IsActive body, pinned and patched.

The fixture preserves src/Ai/Class/Warlock/WarlockTriggers.cpp exactly as the
realm pin ships it. The harness extracts LifeTapTrigger::IsActive from that
fixture twice - as pinned, and with patches/mod-playerbots/0022 applied - and
compiles each against a small model of what the body reads: the bot's health
and mana percentages, whether it has a mana bar, whether it is in combat, and
the three health thresholds from playerbots.conf.dist (45, 65, 85).

The live failure: a warlock tapped its health down to just above 45% between
pulls, where eating (which starts only below 45%) never began, and opened the
next fight at half health. No realm, database or network is used.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCH = (
    HERE.parent
    / "patches/mod-playerbots/0022-a-warlock-life-taps-only-from-a-health-floor.patch"
)
FIXTURE = HERE / "fixtures/life_tap/WarlockTriggers.cpp"
TARGET = Path("src/Ai/Class/Warlock/WarlockTriggers.cpp")
SIGNATURE = "bool LifeTapTrigger::IsActive()"

HARNESS = r"""
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using uint8 = uint8_t;
using uint32 = uint32_t;

struct Bot
{
    bool combat = false;
    bool IsInCombat() const { return combat; }
};

struct Config
{
    uint32 CriticalHealth = 25, LowHealth = 45, MediumHealth = 65, AlmostFullHealth = 85;
};
static Config sPlayerbotAIConfig;

static int g_health = 100;
static int g_mana = 100;
static bool g_hasMana = true;

template <typename T>
static T Read(char const* name)
{
    if (!std::strcmp(name, "health"))
        return static_cast<T>(g_health);
    if (!std::strcmp(name, "mana"))
        return static_cast<T>(g_mana);
    if (!std::strcmp(name, "has mana"))
        return static_cast<T>(g_hasMana);
    std::abort();
}

#define AI_VALUE2(type, name, param) Read<type>(name)

class LifeTapTrigger
{
public:
    Bot* bot = nullptr;
    bool IsActive();
};

BODY

int main(int argc, char** argv)
{
    if (argc != 5)
        return 2;
    Bot b;
    b.combat = std::atoi(argv[1]) != 0;
    g_health = std::atoi(argv[2]);
    g_mana = std::atoi(argv[3]);
    g_hasMana = std::atoi(argv[4]) != 0;
    LifeTapTrigger t;
    t.bot = &b;
    std::printf("%d\n", t.IsActive() ? 1 : 0);
    return 0;
}
"""


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def build(root, source, name):
    compiler = shutil.which(os.environ.get("CXX", "g++")) or shutil.which("c++")
    if compiler is None:
        raise RuntimeError("a C++ compiler is required for this replay")
    harness = root / (name + ".cpp")
    harness.write_text(HARNESS.replace("BODY", function(source, SIGNATURE)))
    binary = root / name
    built = subprocess.run(  # noqa: S603 - fixed compiler invocation on harness text
        [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-parameter",
            str(harness),
            "-o",
            str(binary),
        ],
        check=False,
        capture_output=True,
        text=True,
        timeout=120,
    )
    if built.returncode != 0:
        raise RuntimeError("harness does not compile:\n" + built.stderr)
    return binary


def taps(binary, in_combat, health, mana, has_mana=True):
    done = subprocess.run(  # noqa: S603 - the replay binary built just above
        [str(binary), str(int(in_combat)), str(health), str(mana), str(int(has_mana))],
        check=True,
        capture_output=True,
        text=True,
        timeout=15,
    )
    return done.stdout.strip() == "1"


class LifeTapFloor(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        root = Path(cls.temp.name)
        if not PATCH.exists():
            raise RuntimeError("the life tap patch is required for this replay")
        git = shutil.which("git")
        if git is None:
            raise RuntimeError("git required to apply the repository patch")

        pinned_root = root / "pinned"
        (pinned_root / TARGET).parent.mkdir(parents=True)
        shutil.copyfile(FIXTURE, pinned_root / TARGET)
        cls.pinned = build(pinned_root, (pinned_root / TARGET).read_text(), "pinned")

        patched_root = root / "patched"
        (patched_root / TARGET).parent.mkdir(parents=True)
        shutil.copyfile(FIXTURE, patched_root / TARGET)
        applied = subprocess.run(  # noqa: S603 - repository patch applied to a temporary fixture
            [git, "apply", str(PATCH)],
            cwd=patched_root,
            check=False,
            capture_output=True,
            text=True,
            timeout=15,
        )
        if applied.returncode != 0:
            raise RuntimeError(
                "patch does not apply to the pinned fixture:\n" + applied.stderr
            )
        cls.patched = build(
            patched_root, (patched_root / TARGET).read_text(), "patched"
        )

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_pinned_taps_down_to_half_health_between_pulls(self):
        # The failure this patch is for: out of combat at 50% the pinned
        # trigger taps again, and eating starts only below 45%.
        self.assertTrue(taps(self.pinned, False, 50, 60))
        self.assertTrue(taps(self.pinned, True, 50, 60))

    def test_out_of_combat_taps_only_from_almost_full_health(self):
        self.assertFalse(taps(self.patched, False, 50, 60))
        self.assertFalse(taps(self.patched, False, 84, 60))
        self.assertTrue(taps(self.patched, False, 85, 60))
        self.assertTrue(taps(self.patched, False, 100, 10))

    def test_in_combat_the_filler_stops_at_medium_health(self):
        self.assertFalse(taps(self.patched, True, 50, 60))
        self.assertFalse(taps(self.patched, True, 64, 60))
        self.assertTrue(taps(self.patched, True, 65, 60))

    def test_mana_rules_are_unchanged(self):
        for binary in (self.pinned, self.patched):
            self.assertFalse(taps(binary, False, 100, 85))
            self.assertFalse(taps(binary, True, 100, 90))
            self.assertFalse(taps(binary, False, 100, 10, has_mana=False))


if __name__ == "__main__":
    unittest.main()
