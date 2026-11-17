/*
 * The Zul'Farrak Ukorz watchdog recovers a stuck walk before ending the run.
 *
 * The decision is pinned by test_zf_steps.cpp. This pins the adapter: a safe
 * recovery is attempted only outside combat and regrouping, and walks the tank
 * along a grounded path toward the nearest living Ukorz.
 */

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

std::string ReadSource()
{
    std::ifstream file("src/mod_overseer.cpp");
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

std::string Between(std::string const& source, std::string const& start,
                    std::string const& end)
{
    std::string::size_type first = source.find(start);
    if (first == std::string::npos)
        return {};
    first += start.size();
    std::string::size_type last = source.find(end, first);
    return last == std::string::npos ? std::string() : source.substr(first, last - first);
}

bool Has(std::string const& text, char const* needle)
{
    return text.find(needle) != std::string::npos;
}
}  // namespace

int main()
{
    std::string const source = ReadSource();
    Check("the module source was read", !source.empty());
    std::string const step = Between(source, "bool StepZulFarrakSteps(",
                                     "bool StepGuildRunStall(");
    Check("the Zul'Farrak step adapter exists", !step.empty());
    Check("the adapter handles a recovery verdict",
          Has(step, "ZulFarrakStepVerdict::Recover"));
    Check("the guard excludes combat, regrouping and a rest hold",
          Has(step, "walk.pauseMovement = poll.bossInCombat || regrouping || run.brainHeld") &&
              Has(step, "member->IsInCombat()"));
    Check("it finds the nearest living Ukorz", Has(step, "ukorz = candidate"));
    Check("it computes a grounded step to Ukorz",
          Has(step, "GroundedStep(tank, destination, step)"));
    Check("it sends the tank toward that step",
          Has(step, "tank->GetMotionMaster()->MovePoint(ZF_UKORZ_RECOVERY_POINT_ID"));
    Check("recovery is reported with the run and measured distance",
          Has(step, "walk recovery toward Ukorz") && Has(step, "walk.yards"));
    Check("the drive passes regrouping state to the recovery guard",
          Has(source, "StepZulFarrakSteps(run, poll, regrouping, now, why)"));

    if (failures)
        return 1;
    std::puts("the Zul'Farrak Ukorz walk recovery is wired to grounded movement");
    return 0;
}
