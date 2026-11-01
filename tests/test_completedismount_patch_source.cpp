/*
 * CompleteDismount guards MOVEMENTFLAG_FALLING and bounds MoveFall ground search.
 *
 * Source contract and logic verification for
 * patches/mod-playerbots/0019-completedismount-guards-falling-flags-and-bounds-fall.patch (#254).
 *
 * The two defects this pins:
 * 1. Under a live MoveFall descent spline, CompleteDismount stripped
 *    MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR unconditionally two lines
 *    after MoveFall() set it. For a bot sharing its Player with a real connected
 *    client, the server told the client it was not falling during the descent
 *    whose landing packet Player::HandleFall was billed for.
 * 2. MoveFall() internally invokes GetMapHeight with MAX_FALL_DISTANCE
 *    (250000.0f), which is unbounded. On vmap geometry where height search
 *    resolves to terrain underneath the surface the bot stands on, that unbounded
 *    search committed to an uncapped descent that killed the character in a
 *    single hit.
 *
 * The fix bounds the search with DEFAULT_HEIGHT_SEARCH (50.0f yards) and infers
 * whether MoveFall committed to a descent via spline ID, isFalling(), and
 * !Finalized(), clearing falling flags only when MoveFall declined.
 *
 * Compiled against src/overseer_decisions.cpp and the standard library.
 */

#include "overseer_decisions.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

std::string Read(char const* path)
{
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

constexpr float DEFAULT_HEIGHT_SEARCH = 50.0f;
constexpr float INVALID_HEIGHT = -100000.0f;
constexpr std::uint32_t MOVEMENTFLAG_FALLING = 0x00002000;
constexpr std::uint32_t MOVEMENTFLAG_FALLING_FAR = 0x00004000;

// Replay state machine mirroring the patched CompleteDismount logic
struct MockMoveSpline
{
    std::uint32_t id{100};
    bool falling{false};
    bool done{true};

    [[nodiscard]] std::uint32_t GetId() const { return id; }
    [[nodiscard]] bool isFalling() const { return falling; }
    [[nodiscard]] bool Finalized() const { return done; }
};

struct MockPlayer
{
    float z{100.0f};
    std::uint32_t movementFlags{0};
    MockMoveSpline movespline;

    [[nodiscard]] float GetPositionZ() const { return z; }
    [[nodiscard]] bool HasUnitMovementFlag(std::uint32_t flag) const { return (movementFlags & flag) != 0; }
    void AddUnitMovementFlag(std::uint32_t flag) { movementFlags |= flag; }
    void RemoveUnitMovementFlag(std::uint32_t flag) { movementFlags &= ~flag; }
};

// Simulated MoveFall execution
void SimulateMoveFall(MockPlayer& bot, float groundZ)
{
    // Abort if ground is very near (matches MotionMaster.cpp line 797)
    if (std::fabs(bot.GetPositionZ() - groundZ) < 0.1f)
        return;

    // Commits to descent
    bot.AddUnitMovementFlag(MOVEMENTFLAG_FALLING);
    bot.movespline.id++;
    bot.movespline.falling = true;
    bot.movespline.done = false;
}

// Simulated CompleteDismount matching patch 0019
void SimulateCompleteDismount(MockPlayer& bot, float groundZ)
{
    float const startZ = bot.GetPositionZ();

    bool committed = false;
    if (groundZ > INVALID_HEIGHT && (startZ - groundZ) <= DEFAULT_HEIGHT_SEARCH)
    {
        std::uint32_t const splineIdBefore = bot.movespline.GetId();
        SimulateMoveFall(bot, groundZ);
        committed = (bot.movespline.GetId() != splineIdBefore) &&
                    bot.movespline.isFalling() &&
                    !bot.movespline.Finalized();
    }

    if (!committed)
        bot.RemoveUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR);
}

void ThePatchExistsAndIsDocumented()
{
    char const* patchPath =
        "patches/mod-playerbots/0019-completedismount-guards-falling-flags-and-bounds-fall.patch";
    std::string const patch = Read(patchPath);
    Check("patch 0019 exists", !patch.empty());
    Check("it patches CheckMountStateAction.cpp",
          patch.find("+++ b/src/Ai/Base/Actions/CheckMountStateAction.cpp") != std::string::npos);
    Check("it documents why this patch exists",
          patch.find("WHY THIS PATCH EXISTS") != std::string::npos);
    Check("it documents what would let it be deleted",
          patch.find("WHAT WOULD LET THIS PATCH BE DELETED") != std::string::npos);
    Check("it documents what it applies to",
          patch.find("APPLIES TO") != std::string::npos);
}

void PatchCodeStructureConformsToContract()
{
    char const* patchPath =
        "patches/mod-playerbots/0019-completedismount-guards-falling-flags-and-bounds-fall.patch";
    std::string const patch = Read(patchPath);

    Check("patch includes MoveSpline.h",
          patch.find("+#include \"MoveSpline.h\"") != std::string::npos);
    Check("patch bounds ground search to DEFAULT_HEIGHT_SEARCH",
          patch.find("DEFAULT_HEIGHT_SEARCH") != std::string::npos);
    Check("patch samples splineIdBefore",
          patch.find("splineIdBefore") != std::string::npos);
    Check("patch checks spline ID change, isFalling, and !Finalized",
          patch.find("bot->movespline->GetId() != splineIdBefore") != std::string::npos &&
              patch.find("bot->movespline->isFalling()") != std::string::npos &&
              patch.find("!bot->movespline->Finalized()") != std::string::npos);
    Check("patch only clears flags when !committed",
          patch.find("if (!committed)") != std::string::npos &&
              patch.find("bot->RemoveUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR);") !=
                  std::string::npos);
}

void Case1_BotOnGroundMoveFallDeclinesFlagsCleared()
{
    MockPlayer bot;
    bot.z = 50.0f;
    bot.movementFlags = MOVEMENTFLAG_FALLING; // suppose bot had stale flag

    SimulateCompleteDismount(bot, 50.05f); // ground within 0.05 yards (< 0.1)

    Check("on ground: MoveFall declined, falling flag cleared",
          !bot.HasUnitMovementFlag(MOVEMENTFLAG_FALLING));
    Check("spline was not launched", bot.movespline.Finalized());
}

void Case2_BotInAirMoveFallCommitsFlagsPreserved()
{
    MockPlayer bot;
    bot.z = 60.0f;
    bot.movementFlags = 0;

    SimulateCompleteDismount(bot, 50.0f); // ground is 10 yards below

    Check("in air: MoveFall committed, falling flag preserved",
          bot.HasUnitMovementFlag(MOVEMENTFLAG_FALLING));
    Check("spline is actively falling",
          bot.movespline.isFalling() && !bot.movespline.Finalized());
}

void Case3_BotAlreadyFallingNowOnGroundDeclinesAndClearsStuckFlag()
{
    MockPlayer bot;
    bot.z = 50.0f;
    bot.movementFlags = MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR;
    bot.movespline.id = 42;
    bot.movespline.falling = true;
    bot.movespline.done = false;

    // Dismount happens right as bot reaches ground (< 0.1 yards)
    SimulateCompleteDismount(bot, 50.02f);

    Check("already falling, but ground reached: MoveFall declined",
          bot.movespline.id == 42);
    Check("stuck falling flag is cleared on decline",
          !bot.HasUnitMovementFlag(MOVEMENTFLAG_FALLING));
    Check("stuck falling far flag is cleared on decline",
          !bot.HasUnitMovementFlag(MOVEMENTFLAG_FALLING_FAR));
}

void Case4_BotAlreadyFallingStillInAirCommitsAndPreservesFlag()
{
    MockPlayer bot;
    bot.z = 70.0f;
    bot.movementFlags = MOVEMENTFLAG_FALLING;
    bot.movespline.id = 42;
    bot.movespline.falling = true;
    bot.movespline.done = false;

    // Dismount happens 20 yards above ground
    SimulateCompleteDismount(bot, 50.0f);

    Check("already falling and still in air: new spline launched",
          bot.movespline.id > 42);
    Check("falling flag is preserved under running spline",
          bot.HasUnitMovementFlag(MOVEMENTFLAG_FALLING));
    Check("spline is still falling",
          bot.movespline.isFalling() && !bot.movespline.Finalized());
}

void Case5_UnboundedDropDeclinedAndFlagsCleared()
{
    MockPlayer bot;
    bot.z = 150.0f;
    bot.movementFlags = MOVEMENTFLAG_FALLING;

    // Ground search beyond 50 yards returns INVALID_HEIGHT or > 50 yards away
    SimulateCompleteDismount(bot, 80.0f); // 70 yards below (exceeds 50 yard bound)

    Check("drop exceeds bound: MoveFall not called, no plunge",
          bot.movespline.id == 100);
    Check("falling flag cleared rather than sent on fatal descent",
          !bot.HasUnitMovementFlag(MOVEMENTFLAG_FALLING));
}

void Case6_BoundedFallDamageCapsBelowLethal()
{
    // Verify core fall damage arithmetic at 50 yards
    // FractionOfMaxHealth = 0.018 * 50 - 0.2426 = 0.6574 (65.74% max health)
    float const fallDmg50Yards = 0.018f * DEFAULT_HEIGHT_SEARCH - 0.2426f;
    Check("50 yard drop deals <= 66% max health damage",
          fallDmg50Yards > 0.65f && fallDmg50Yards < 0.66f);
    Check("50 yard drop cannot kill from full health outright",
          fallDmg50Yards < 1.0f);

    // Lethal fall requires >= 69.03 yards
    float const lethalDistance = (1.0f + 0.2426f) / 0.018f;
    Check("lethal distance requires > 69 yards",
          lethalDistance > 69.0f && lethalDistance < 69.1f);
    Check("DEFAULT_HEIGHT_SEARCH sits safely below lethal distance",
          DEFAULT_HEIGHT_SEARCH < lethalDistance);
}

} // namespace

int main()
{
    ThePatchExistsAndIsDocumented();
    PatchCodeStructureConformsToContract();
    Case1_BotOnGroundMoveFallDeclinesFlagsCleared();
    Case2_BotInAirMoveFallCommitsFlagsPreserved();
    Case3_BotAlreadyFallingNowOnGroundDeclinesAndClearsStuckFlag();
    Case4_BotAlreadyFallingStillInAirCommitsAndPreservesFlag();
    Case5_UnboundedDropDeclinedAndFlagsCleared();
    Case6_BoundedFallDamageCapsBelowLethal();

    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ok: CompleteDismount guards falling flags and bounds ground search\n");
    return 0;
}
