/*
 * Source contract for the chosen quest's hand-in (#180).
 *
 * The decision is proved in test_chosen_quest_hand_in.cpp. What only the
 * adapter can get wrong is whether DriveChosenQuest ASKS it and acts on the
 * hand-in: before #180 it left a COMPLETE quest to upstream's own quest-giver
 * search, which refuses a reward it cannot store and gives the quest up after
 * five minutes, so the family carried finished quests and the drive re-issued
 * the walk back to the giver for ever. A decision nothing feeds is the failure
 * this repository has shipped before, so the wiring is pinned as text.
 */

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
int Fail(char const* what)
{
    std::cerr << what << "\n";
    return EXIT_FAILURE;
}

// The text of the member function that starts at `signature`, up to the next
// line that closes a member function at the class's indentation.
std::string Body(std::string const& text, char const* signature)
{
    std::size_t const start = text.find(signature);
    if (start == std::string::npos)
        return std::string();
    std::size_t const end = text.find("\n    }\n", start);
    return end == std::string::npos ? std::string() : text.substr(start, end - start);
}
}  // namespace

int main()
{
    std::ifstream source("src/mod_overseer.cpp");
    if (!source)
        return Fail("cannot read src/mod_overseer.cpp");
    std::string const text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    std::string const drive = Body(text, "bool DriveChosenQuest(");
    if (drive.empty())
        return Fail("cannot find DriveChosenQuest");

    std::size_t const asks = drive.find("OverseerDecisions::ChosenQuestStepFor(");
    if (asks == std::string::npos)
        return Fail("DriveChosenQuest must decide its step with ChosenQuestStepFor");

    std::size_t const handIn = drive.find("ChosenQuestStep::HandIn", asks);
    std::size_t const handInCall = drive.find("HandInChosenQuest(", asks);
    if (handIn == std::string::npos || handInCall == std::string::npos)
        return Fail("DriveChosenQuest must hand a COMPLETE quest in when the decision says so");

    // The readiness is the quest errand's own turn-in check, so a drive hand-in
    // refuses exactly what an operator's `turnin quest:<id>` would refuse, and
    // it asks whether the reward the turn-in will take fits (#973).
    std::string const ready = Body(text, "bool ChosenQuestHandInReady(");
    if (ready.empty())
        return Fail("cannot find ChosenQuestHandInReady");
    for (char const* fed : {"OverseerDecisions::QuestErrandRefusal(",
                            "QuestErrandVerb::TurnIn",
                            "ChooseQuestRewardFor(bot, quest)",
                            "CanRewardQuest(quest, reward.index, false)"})
        if (ready.find(fed) == std::string::npos)
        {
            std::cerr << "ChosenQuestHandInReady must use: " << fed << "\n";
            return EXIT_FAILURE;
        }

    // The hand-in is read back: it reports a turn-in only once the core has
    // recorded the reward.
    std::string const give = Body(text, "bool HandInChosenQuest(");
    if (give.empty())
        return Fail("cannot find HandInChosenQuest");
    std::size_t const reward = give.find("RewardQuestChosen(");
    std::size_t const readBack = give.find("GetQuestRewardStatus(", reward);
    if (reward == std::string::npos || readBack == std::string::npos)
        return Fail("HandInChosenQuest must reward the quest and read the reward back");

    // The drive must observe that same rewarded state and release the aim
    // before another poll can reassert the completed errand.
    std::size_t const driveQuests = text.find("void DriveQuests(");
    if (driveQuests == std::string::npos)
        return Fail("cannot find DriveQuests");
    std::size_t const rewardedAim = text.find("GetQuestRewardStatus(aim)", driveQuests);
    std::size_t const clearAim = text.find("ClearAim(name)", rewardedAim);
    std::size_t const driveChosen = text.find("DriveChosenQuest(", rewardedAim);
    if (rewardedAim == std::string::npos || clearAim == std::string::npos ||
        driveChosen == std::string::npos || clearAim > driveChosen)
        return Fail("DriveQuests must release a rewarded aim before driving it again");

    // No inline copy of the old steady-lease guard is left to bypass the step.
    if (drive.find("if (working == questId && !aimChanged)") != std::string::npos)
        return Fail("DriveChosenQuest must not keep the old inline lease guard");

    std::cout << "DriveChosenQuest hands a complete chosen quest in\n";
    return EXIT_SUCCESS;
}
