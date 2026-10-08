/*
 * A rename of an OFFLINE character (2026-10-08): the gate rows, one by one,
 * and the plan that leaves the character logged out.
 *
 * Compiles against the pure decision file and nothing from AzerothCore.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <cstring>
#include <vector>

using OverseerDecisions::RenameOfflineFacts;
using OverseerDecisions::RenameOfflineVerdict;
using OverseerDecisions::RenameOfflineVerdictFor;
using OverseerDecisions::RenamePlan;
using OverseerDecisions::RenamePlanFor;
using OverseerDecisions::RenameRoute;
using OverseerDecisions::RenameStep;
using OverseerDecisions::RenameSteps;

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

bool Says(RenameOfflineVerdict const& v, char const* text)
{
    return v.route == RenameRoute::Refuse && std::strcmp(v.reason, text) == 0;
}

// An offline recruit of a listed guild with a free, valid name, the switch on,
// and room under the cap.
RenameOfflineFacts Ready()
{
    RenameOfflineFacts f;
    f.switchOn = true;
    f.exists = true;
    f.guildListed = true;
    f.nameValid = true;
    f.actsCap = 4;
    return f;
}

void TheGateRows()
{
    RenameOfflineFacts f = Ready();
    Check("offline, switch on, everything fine: a headless act",
          RenameOfflineVerdictFor(f).route == RenameRoute::HeadlessAct);

    f = Ready();
    f.online = true;
    Check("online: the existing path", RenameOfflineVerdictFor(f).route == RenameRoute::ExistingPath);

    f = Ready();
    f.online = true;
    f.switchOn = false;
    Check("online with the switch off: still the existing path",
          RenameOfflineVerdictFor(f).route == RenameRoute::ExistingPath);

    f = Ready();
    f.switchOn = false;
    Check("offline and switch off: target not online, as before",
          Says(RenameOfflineVerdictFor(f), "target not online"));

    f = Ready();
    f.exists = false;
    Check("no such character", Says(RenameOfflineVerdictFor(f), "no such character"));

    f = Ready();
    f.nameTaken = true;
    Check("name taken", Says(RenameOfflineVerdictFor(f), "name already taken"));

    f = Ready();
    f.nameValid = false;
    Check("invalid name", Says(RenameOfflineVerdictFor(f), "invalid name"));

    f = Ready();
    f.sameName = true;
    Check("same name", Says(RenameOfflineVerdictFor(f), "that is already its name"));

    f = Ready();
    f.guildListed = false;
    Check("not a bot of a listed guild", Says(RenameOfflineVerdictFor(f), "not a bot of a listed guild"));

    f = Ready();
    f.clientAttached = true;
    Check("a client holds it", Says(RenameOfflineVerdictFor(f), "a real client holds this character"));

    f = Ready();
    f.rowInFlight = true;
    Check("one at a time per character",
          Says(RenameOfflineVerdictFor(f), "another headless act for this character is still in flight"));
}

void TheCap()
{
    RenameOfflineFacts f = Ready();
    f.actsInFlight = 3;
    Check("one under the cap goes", RenameOfflineVerdictFor(f).route == RenameRoute::HeadlessAct);
    f.actsInFlight = 4;
    Check("at the cap: refused with a retry",
          Says(RenameOfflineVerdictFor(f), "too many headless acts running; retry in a minute"));
    f.actsInFlight = 9;
    Check("over the cap: refused", RenameOfflineVerdictFor(f).route == RenameRoute::Refuse);
    f = Ready();
    f.actsCap = 0;
    Check("a cap of zero admits nothing", RenameOfflineVerdictFor(f).route == RenameRoute::Refuse);
}

void ARosterCharacterIsNeverRenamedThisWay()
{
    RenameOfflineFacts f = Ready();
    f.inFamily = true;
    Check("a roster or family character is refused",
          Says(RenameOfflineVerdictFor(f), "a roster character is not renamed while offline"));
    f.guildListed = false;
    Check("and refused as a roster character first", RenameOfflineVerdictFor(f).route == RenameRoute::Refuse);
    f = Ready();
    f.inFamily = true;
    f.online = true;
    Check("online it is the existing path's business",
          RenameOfflineVerdictFor(f).route == RenameRoute::ExistingPath);
}

void ThePlanLeavesItLoggedOut()
{
    Check("offline origin plans a stay-out", RenamePlanFor(false, true, true, true) == RenamePlan::EvictRenameStayOut);
    Check("online origin keeps the log-back-in plan", RenamePlanFor(false, true, true, false) == RenamePlan::EvictThenRename);
    Check("a refusal stays a refusal", RenamePlanFor(true, true, true, true) == RenamePlan::Refuse);
    Check("a real client is never a stay-out", RenamePlanFor(false, false, true, true) == RenamePlan::RenameThenKick);

    std::vector<RenameStep> const& steps = RenameSteps(RenamePlan::EvictRenameStayOut);
    bool logsIn = false;
    for (RenameStep s : steps)
        logsIn = logsIn || s == RenameStep::LogIn;
    Check("no LogIn step", !logsIn);
    Check("out, name, rows", steps.size() == 3 && steps[0] == RenameStep::LogOut &&
                                 steps[1] == RenameStep::WriteName && steps[2] == RenameStep::MoveRows);
}

}  // namespace

int main()
{
    TheGateRows();
    TheCap();
    ARosterCharacterIsNeverRenamedThisWay();
    ThePlanLeavesItLoggedOut();
    if (failures)
        return 1;
    std::printf("ok test_rename_offline\n");
    return 0;
}
