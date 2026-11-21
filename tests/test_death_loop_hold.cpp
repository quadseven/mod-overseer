/*
 * A death loop holds the idle AI off its zone (wow-overseer#2152).
 *
 * A character the bridge has flagged as dying over and over in one zone is
 * held off its own wandering there until it leaves the zone or a bounded time
 * passes. The rule, the flag the bridge writes, each refusal, the bound and
 * the step a poll takes are pinned here one case at a time, so a case that
 * stops holding (or starts) fails by name.
 *
 * Compiled against src/overseer_decisions.cpp and nothing else; the wiring
 * checks read src/mod_overseer.cpp and conf/mod_overseer.conf.dist as text.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace D = OverseerDecisions;

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
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

constexpr int64_t NOW = 1800000000;
constexpr int64_t HOUR = 3600;

// A held character: switched on, flagged in the zone it stands in an hour
// ago, no job, nobody it follows, the default bound.
D::DeathLoopHoldFacts Held()
{
    D::DeathLoopHoldFacts f;
    f.enabled = true;
    f.flagged = true;
    f.flagMapId = 1;
    f.flagZoneId = 1377;
    f.flaggedAt = NOW - HOUR;
    f.mapId = 1;
    f.zoneId = 1377;
    f.now = NOW;
    f.boundSeconds = D::DeathLoopHoldBound(D::DEATH_LOOP_HOLD_DEFAULT_SECONDS);
    f.hasJob = false;
    f.follower = false;
    return f;
}

void TestHolds()
{
    Check("a flagged member in its loop zone inside the bound is held", D::DeathLoopHolds(Held()));
    Check("the reason names the hold",
          D::DeathLoopHoldReason(Held()) == D::DeathLoopHoldWhy::Holds);
    D::DeathLoopHoldFacts just = Held();
    just.flaggedAt = NOW;
    Check("a flag written this second holds", D::DeathLoopHolds(just));
}

void TestDefaultsHoldNobody()
{
    D::DeathLoopHoldFacts f;
    Check("default facts hold nobody", !D::DeathLoopHolds(f));
}

void TestSwitchOff()
{
    D::DeathLoopHoldFacts f = Held();
    f.enabled = false;
    Check("switched off holds nobody", D::DeathLoopHoldReason(f) == D::DeathLoopHoldWhy::SwitchOff);
    f = Held();
    f.boundSeconds = 0;
    Check("a zero bound holds nobody", D::DeathLoopHoldReason(f) == D::DeathLoopHoldWhy::SwitchOff);
}

void TestNotFlagged()
{
    D::DeathLoopHoldFacts f = Held();
    f.flagged = false;
    Check("an unflagged member is not held",
          D::DeathLoopHoldReason(f) == D::DeathLoopHoldWhy::NotFlagged);
    f = Held();
    f.flagZoneId = 0;
    f.zoneId = 0;
    Check("a flag naming zone 0 holds nobody",
          D::DeathLoopHoldReason(f) == D::DeathLoopHoldWhy::NotFlagged);
    f = Held();
    f.flaggedAt = 0;
    Check("a flag with no time holds nobody",
          D::DeathLoopHoldReason(f) == D::DeathLoopHoldWhy::NotFlagged);
    f = Held();
    f.flaggedAt = NOW + 60;
    Check("a flag stamped after now holds nobody, so no clock can stretch the bound",
          D::DeathLoopHoldReason(f) == D::DeathLoopHoldWhy::NotFlagged);
}

void TestJobAndFollower()
{
    D::DeathLoopHoldFacts f = Held();
    f.hasJob = true;
    Check("a member with a job is never held", D::DeathLoopHoldReason(f) == D::DeathLoopHoldWhy::HasJob);
    f = Held();
    f.follower = true;
    Check("a follower is never held", D::DeathLoopHoldReason(f) == D::DeathLoopHoldWhy::Follower);
}

void TestLeftZone()
{
    D::DeathLoopHoldFacts f = Held();
    f.zoneId = 440;
    Check("a member in another zone is not held",
          D::DeathLoopHoldReason(f) == D::DeathLoopHoldWhy::LeftZone);
    f = Held();
    f.mapId = 0;
    Check("a member on another map is not held",
          D::DeathLoopHoldReason(f) == D::DeathLoopHoldWhy::LeftZone);
}

void TestBound()
{
    D::DeathLoopHoldFacts f = Held();
    f.flaggedAt = NOW - D::DEATH_LOOP_HOLD_DEFAULT_SECONDS + 1;
    Check("one second inside the bound still holds", D::DeathLoopHolds(f));
    f.flaggedAt = NOW - D::DEATH_LOOP_HOLD_DEFAULT_SECONDS;
    Check("the hold ends at the bound",
          D::DeathLoopHoldReason(f) == D::DeathLoopHoldWhy::BoundPassed);
    f.flaggedAt = NOW - 30 * 24 * HOUR;
    Check("a month-old flag holds nobody",
          D::DeathLoopHoldReason(f) == D::DeathLoopHoldWhy::BoundPassed);

    Check("the bound defaults to a few hours",
          D::DEATH_LOOP_HOLD_DEFAULT_SECONDS >= 2 * HOUR && D::DEATH_LOOP_HOLD_DEFAULT_SECONDS <= 6 * HOUR);
    Check("a negative bound is no hold", D::DeathLoopHoldBound(-5) == 0);
    Check("a bound inside the maximum is kept", D::DeathLoopHoldBound(2 * HOUR) == 2 * HOUR);
    Check("a huge bound is clamped to the maximum",
          D::DeathLoopHoldBound(int64_t(1) << 40) == D::DEATH_LOOP_HOLD_MAX_SECONDS);

    // A raw bound the adapter forgot to clamp is still clamped by the rule.
    f = Held();
    f.boundSeconds = int64_t(1) << 40;
    f.flaggedAt = NOW - D::DEATH_LOOP_HOLD_MAX_SECONDS;
    Check("no configured bound holds past the maximum",
          D::DeathLoopHoldReason(f) == D::DeathLoopHoldWhy::BoundPassed);
}

void TestReasonNames()
{
    for (D::DeathLoopHoldWhy why :
         {D::DeathLoopHoldWhy::Holds, D::DeathLoopHoldWhy::SwitchOff, D::DeathLoopHoldWhy::NotFlagged,
          D::DeathLoopHoldWhy::HasJob, D::DeathLoopHoldWhy::Follower, D::DeathLoopHoldWhy::LeftZone,
          D::DeathLoopHoldWhy::BoundPassed})
        Check("every reason has a sentence", std::string(D::DeathLoopHoldWhyName(why)) != "unknown");
}

void TestParseFlag()
{
    D::DeathLoopFlag f = D::ParseDeathLoopFlag("{\"map\": 1, \"zone\": 1377}");
    Check("the bridge's value is read", f.ok && f.mapId == 1 && f.zoneId == 1377);
    f = D::ParseDeathLoopFlag("{\"zone\":28,\"map\":0}");
    Check("key order and spacing do not matter", f.ok && f.mapId == 0 && f.zoneId == 28);
    Check("an empty value is no flag", !D::ParseDeathLoopFlag("").ok);
    Check("a bare string is no flag", !D::ParseDeathLoopFlag("\"1:1377\"").ok);
    Check("a value with no zone is no flag", !D::ParseDeathLoopFlag("{\"map\": 1}").ok);
    Check("zone 0 is no flag", !D::ParseDeathLoopFlag("{\"map\": 1, \"zone\": 0}").ok);
    Check("a negative zone is no flag", !D::ParseDeathLoopFlag("{\"map\": 1, \"zone\": -4}").ok);
    Check("a fractional zone is no flag", !D::ParseDeathLoopFlag("{\"map\": 1, \"zone\": 1.5}").ok);
    Check("a zone past 32 bits is no flag",
          !D::ParseDeathLoopFlag("{\"map\": 1, \"zone\": 99999999999}").ok);
    // 2^64 + 1377: a parser that let the 64-bit value wrap would read zone 1377
    // and hold the wrong character. The per-digit 32-bit check refuses it long
    // before the multiply could wrap.
    Check("a zone that would wrap 64 bits to a valid zone is no flag",
          !D::ParseDeathLoopFlag("{\"map\": 1, \"zone\": 18446744073709552993}").ok);
    Check("a 25-digit map is no flag",
          !D::ParseDeathLoopFlag("{\"map\": 1000000000000000000000001, \"zone\": 1377}").ok);
}

void TestSteps()
{
    using S = D::DeathLoopHoldStep;
    using W = D::DeathLoopHoldWhy;
    Check("held and carrying new rpg: take it off", D::DeathLoopHoldStepFor(W::Holds, false, true, false) == S::TakeOff);
    Check("held again after something handed it back: take it off again",
          D::DeathLoopHoldStepFor(W::Holds, true, true, false) == S::TakeOff);
    Check("held and already off: nothing", D::DeathLoopHoldStepFor(W::Holds, true, false, false) == S::Nothing);
    Check("held while another hold has it: nothing, that hold owns new rpg",
          D::DeathLoopHoldStepFor(W::Holds, false, true, true) == S::Nothing);
    Check("left the zone after this rule took it off: hand it back",
          D::DeathLoopHoldStepFor(W::LeftZone, true, false, false) == S::HandBack);
    Check("bound passed: hand it back", D::DeathLoopHoldStepFor(W::BoundPassed, true, false, false) == S::HandBack);
    Check("flag dropped: hand it back", D::DeathLoopHoldStepFor(W::NotFlagged, true, false, false) == S::HandBack);
    Check("switched off: hand it back", D::DeathLoopHoldStepFor(W::SwitchOff, true, false, false) == S::HandBack);
    Check("left the zone mid-walk: wait for the walk's own hold to end",
          D::DeathLoopHoldStepFor(W::LeftZone, true, false, true) == S::Nothing);
    Check("left the zone but this rule never took it off: nothing",
          D::DeathLoopHoldStepFor(W::LeftZone, false, false, false) == S::Nothing);
    Check("now has a job: forget, grant nothing", D::DeathLoopHoldStepFor(W::HasJob, true, false, false) == S::Forget);
    Check("now follows somebody: forget, grant nothing",
          D::DeathLoopHoldStepFor(W::Follower, true, false, true) == S::Forget);
    Check("a follower this rule never touched: nothing",
          D::DeathLoopHoldStepFor(W::Follower, false, true, false) == S::Nothing);
}

// THE WIRING. The adapter must read the switch and the bound, read the
// bridge's flag kind, and take `new rpg` off through the step decision.
void TestWiring()
{
    std::string const src = Read("src/mod_overseer.cpp");
    Check("mod_overseer.cpp was read", !src.empty());
    Check("the adapter reads the switch",
          src.find("\"Overseer.DeathLoopHold.Enable\", true, false") != std::string::npos);
    Check("the adapter reads the bound",
          src.find("\"Overseer.DeathLoopHold.Seconds\"") != std::string::npos);
    Check("the adapter asks the rule", src.find("D::DeathLoopHoldReason(") != std::string::npos);
    Check("the adapter asks the step", src.find("D::DeathLoopHoldStepFor(") != std::string::npos);
    Check("the adapter reads the bridge's flag kind",
          src.find("OverseerDecisions::DEATH_LOOP_FLAG_KIND") != std::string::npos);
    Check("the adapter polls the hold from OnUpdate", src.find("DriveDeathLoopHold();") != std::string::npos);

    std::string const conf = Read("conf/mod_overseer.conf.dist");
    Check("the conf documents the switch, on by default",
          conf.find("#Overseer.DeathLoopHold.Enable = 1") != std::string::npos);
    Check("the conf documents the bound",
          conf.find("#Overseer.DeathLoopHold.Seconds = 10800") != std::string::npos);
    Check("the conf default matches the decisions default", D::DEATH_LOOP_HOLD_DEFAULT_SECONDS == 10800);
}

}  // namespace

int main()
{
    TestHolds();
    TestDefaultsHoldNobody();
    TestSwitchOff();
    TestNotFlagged();
    TestJobAndFollower();
    TestLeftZone();
    TestBound();
    TestReasonNames();
    TestParseFlag();
    TestSteps();
    TestWiring();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("death loop hold: all checks pass\n");
    return 0;
}
