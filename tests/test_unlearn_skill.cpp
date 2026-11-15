/*
 * The command-row grammar and safety gates for unlearning one gathering trade
 * on a non-roster guild character (mod-overseer#1556).
 */
#include "overseer_decisions.h"

#include <cstdio>
#include <string>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Check(char const* name, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", name);
    ++failures;
}
}  // namespace

int main()
{
    Check("valid command is routed", IsUnlearnSkillRow("unlearn-skill skill:182 max:75"));
    Check("malformed command still reaches its parser",
          IsUnlearnSkillRow("unlearn-skill skill:no max:75"));
    Check("a longer verb name is not routed", !IsUnlearnSkillRow("unlearn-skills skill:182 max:75"));

    UnlearnSkillRequest const herbalism =
        ParseUnlearnSkillRequest("unlearn-skill skill:182 max:75");
    Check("herbalism parses", herbalism.skill == 182 && herbalism.max == 75 &&
                                  std::string(herbalism.error).empty());

    UnlearnSkillRequest const skinning =
        ParseUnlearnSkillRequest(" unlearn-skill   max:60 skill:393 ");
    Check("arguments may be reordered", skinning.skill == 393 && skinning.max == 60 &&
                                            std::string(skinning.error).empty());

    char const* const malformed[] = {
        "unlearn-skill", "unlearn-skill skill:182", "unlearn-skill max:75",
        "unlearn-skill skill:0 max:75", "unlearn-skill skill:x max:75",
        "unlearn-skill skill:4294967296 max:75", "unlearn-skill skill:182 max:0",
        "unlearn-skill skill:182 max:76", "unlearn-skill skill:182 max:75 extra:1",
        "unlearn-skill skill:182 skill:186 max:75", "unlearn-skill skill:182 max:75 max:60",
    };
    for (char const* command : malformed)
    {
        UnlearnSkillRequest const request = ParseUnlearnSkillRequest(command);
        Check(command, request.skill == 0 && request.max == 0 &&
                           std::string(request.error) == UnlearnSkillRefusal::Malformed);
    }

    Check("outside the natural guild is refused",
          DecideUnlearnSkill(false, false, 182, true, 20, 75) ==
              UnlearnSkillAction::NotNaturalGuild);
    Check("a roster character is refused",
          DecideUnlearnSkill(true, true, 182, true, 20, 75) ==
              UnlearnSkillAction::RosterCharacter);
    Check("a roster character is refused even outside the natural guild",
          DecideUnlearnSkill(false, true, 182, true, 20, 75) ==
              UnlearnSkillAction::RosterCharacter);
    Check("a crafting profession is refused",
          DecideUnlearnSkill(true, false, 164, true, 20, 75) ==
              UnlearnSkillAction::NotGathering);
    Check("a secondary skill is refused",
          DecideUnlearnSkill(true, false, 356, true, 20, 75) ==
              UnlearnSkillAction::NotGathering);
    Check("an absent gathering skill is already done",
          DecideUnlearnSkill(true, false, 393, false, 0, 75) ==
              UnlearnSkillAction::AlreadyAbsent);
    Check("a live value above the ceiling is refused",
          DecideUnlearnSkill(true, false, 186, true, 76, 75) ==
              UnlearnSkillAction::AboveCeiling);
    Check("a live value at the ceiling may be unlearned",
          DecideUnlearnSkill(true, false, 186, true, 75, 75) ==
              UnlearnSkillAction::ReadyToUnlearn);

    if (failures)
        return 1;
    std::printf("ok test_unlearn_skill\n");
    return 0;
}
