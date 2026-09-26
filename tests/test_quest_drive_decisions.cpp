#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>

namespace
{
int failures = 0;

void Check(char const* what, bool got, bool want)
{
    if (got == want)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", what, got ? "true" : "false",
                want ? "true" : "false");
    ++failures;
}

void ACampaignKeepsTheLeaderForItsFamily()
{
    using OverseerDecisions::QuestDriveMayTakeCampaignLeader;
    Check("idle campaign", QuestDriveMayTakeCampaignLeader(false), true);
    Check("reset", QuestDriveMayTakeCampaignLeader(true), false);
    Check("recovering", QuestDriveMayTakeCampaignLeader(true), false);
}

void QuestTargetsStayWithinTheYoungestMembersLevelGap()
{
    using OverseerDecisions::QuestDriveTargetFitsYoungest;
    Check("three above", QuestDriveTargetFitsYoungest(13, 16, 0, 3), true);
    Check("four above by quest level", QuestDriveTargetFitsYoungest(13, 17, 0, 3), false);
    Check("four above by minimum level", QuestDriveTargetFitsYoungest(13, 0, 17, 3), false);
    Check("quest level is the higher bound", QuestDriveTargetFitsYoungest(13, 17, 14, 3), false);
    Check("no youngest", QuestDriveTargetFitsYoungest(0, 0, 0, 3), true);
}
}  // namespace

int main()
{
    ACampaignKeepsTheLeaderForItsFamily();
    QuestTargetsStayWithinTheYoungestMembersLevelGap();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("quest drive respects campaign and youngest-member decisions\n");
    return EXIT_SUCCESS;
}
