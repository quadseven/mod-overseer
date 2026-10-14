#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Check(char const* label, bool got, bool wanted)
{
    if (got == wanted)
        return;
    std::printf("FAIL %s: got %s, wanted %s\n", label, got ? "true" : "false",
                wanted ? "true" : "false");
    ++failures;
}

void CheckText(char const* label, std::string const& got, char const* wanted)
{
    if (got == wanted)
        return;
    std::printf("FAIL %s: got '%s', wanted '%s'\n", label, got.c_str(), wanted);
    ++failures;
}
}

int main()
{
    // The upgrade pass equips EQUIP, REPLACE and BAD_EQUIP, and nothing else
    // (EquipAction::SelectInventoryItemsToEquip at the pinned mod-playerbots).
    Check("none is not kept", UpgradePassKeeps(UPGRADE_USAGE_NONE), false);
    Check("equip is kept", UpgradePassKeeps(UPGRADE_USAGE_EQUIP), true);
    Check("replace is kept", UpgradePassKeeps(UPGRADE_USAGE_REPLACE), true);
    Check("bad equip is kept", UpgradePassKeeps(UPGRADE_USAGE_BAD_EQUIP), true);
    Check("broken equip is not kept", UpgradePassKeeps(4), false);
    Check("ammo is not kept", UpgradePassKeeps(13), false);

    // The live case: the core would put the girdle on, the bot's own pass
    // would put its own belt back. Refused before the whisper, row unchanged.
    EquipVerdict refused = EquipUpgradeVerdict(UPGRADE_USAGE_NONE,
        "Girdle of the Blindwatcher", 41.5f, "Bandit Cinch", 57.25f);
    Check("a piece the bot would undo is not whispered", refused.whisper, false);
    CheckText("a piece the bot would undo ends unchanged", refused.row.status, "unchanged");
    CheckText("detail names the kept piece and both scores", refused.row.detail,
              "the bot keeps Bandit Cinch: Girdle of the Blindwatcher scores 41.50 against 57.25");

    // An empty slot the bot still would not fill reads as an empty slot, not
    // as a blank name.
    EquipVerdict empty = EquipUpgradeVerdict(UPGRADE_USAGE_NONE, "Bloody Apron", 0.0f, "", 0.0f);
    Check("empty slot refusal is not whispered", empty.whisper, false);
    CheckText("empty slot refusal ends unchanged", empty.row.status, "unchanged");
    CheckText("empty slot refusal says so", empty.row.detail,
              "the bot keeps an empty slot: Bloody Apron scores 0.00 against 0.00");

    // A piece the bot would keep is whispered, and the row waits on #920's
    // equipment read-back like before.
    for (uint32_t usage : {UPGRADE_USAGE_EQUIP, UPGRADE_USAGE_REPLACE, UPGRADE_USAGE_BAD_EQUIP})
    {
        EquipVerdict kept = EquipUpgradeVerdict(usage, "Rhahk'Zor's Hammer", 90.0f,
                                                "Large Club", 20.0f);
        Check("a piece the bot would keep is whispered", kept.whisper, true);
        CheckText("a whispered piece waits on the read-back", kept.row.status, "verifying");
        CheckText("a whispered piece carries no detail yet", kept.row.detail, "");
    }

    // The status assigns to the command loop's `char const*` row status.
    char const* rowStatus = "verifying";
    rowStatus = refused.row.status;
    CheckText("verdict status assigns to the row status", rowStatus, "unchanged");

    return failures ? 1 : 0;
}
