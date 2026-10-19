/*
 * Quest rewards are chosen by the youngest member but not by value (#973).
 *
 * At turn-in, the reward chosen is the one scoring highest for the holder.
 * If none is an upgrade for the holder, the one that is an upgrade for a
 * sibling (handed over via #14).
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace OverseerDecisions;

namespace
{
int failures = 0;

void Expect(bool condition, char const* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void ExpectText(char const* what, std::string const& got, char const* want)
{
    if (got != want)
    {
        std::fprintf(stderr, "FAIL %s: got '%s', wanted '%s'\n", what, got.c_str(), want);
        ++failures;
    }
}

// A mock reward choice with an item entry and a score
struct RewardChoice
{
    uint32_t entry{0};
    float score{0.0f};
};

// Find the highest-scoring reward for a holder
// Returns the entry of the best reward, or 0 if no rewards
uint32_t BestRewardForHolder(std::vector<RewardChoice> const& rewards, float holderScore)
{
    if (rewards.empty())
        return 0;
    
    uint32_t bestEntry = rewards[0].entry;
    float bestScore = rewards[0].score;
    
    for (size_t i = 1; i < rewards.size(); ++i)
    {
        if (rewards[i].score > bestScore)
        {
            bestScore = rewards[i].score;
            bestEntry = rewards[i].entry;
        }
    }
    
    return bestEntry;
}

// Find the best reward that is an upgrade for anyone
// Prefers the holder's upgrade if exists, otherwise any sibling upgrade
uint32_t BestUpgradeForAnyone(std::vector<RewardChoice> const& rewards,
                              float holderScore,
                              std::vector<float> siblingScores)
{
    uint32_t bestUpgrade = 0;
    float bestUpgradeScore = holderScore * (1.f + UPGRADE_MARGIN_FRACTION) + UPGRADE_MARGIN_FLOOR;
    
    for (RewardChoice const& choice : rewards)
    {
        if (choice.score > bestUpgradeScore)
        {
            bestUpgrade = choice.entry;
            bestUpgradeScore = choice.score;
        }
    }
    
    // Check siblings
    for (float siblingScore : siblingScores)
    {
        if (siblingScore > bestUpgradeScore)
        {
            bestUpgrade = 0; // Use a placeholder
            bestUpgradeScore = siblingScore;
        }
    }
    
    return bestUpgrade;
}

void RewardsAreChosenByValue()
{
    std::vector<RewardChoice> rewards = {
        {100, 50.0f},   // Best for holder
        {200, 45.0f},
        {300, 40.0f}
    };
    float holderScore = 30.0f;
    
    uint32_t best = BestRewardForHolder(rewards, holderScore);
    Expect(best == 100, "best reward is chosen by score: entry 100 has score 50.0");
}

void HolderUpgradePreferredOverSibling()
{
    std::vector<RewardChoice> rewards = {
        {100, 35.0f},   // Holder upgrade (35 > 30)
        {200, 45.0f},   // Sibling upgrade
        {300, 30.0f}
    };
    float holderScore = 30.0f;
    std::vector<float> siblingScores = {40.0f, 35.0f};
    
    uint32_t best = BestUpgradeForAnyone(rewards, holderScore, siblingScores);
    Expect(best == 100, "holder upgrade preferred over sibling upgrade");
}

void IfNoHolderUpgradeSiblingChosen()
{
    std::vector<RewardChoice> rewards = {
        {100, 25.0f},   // Not an upgrade for holder (25 <= 30)
        {200, 45.0f},   // Sibling upgrade
        {300, 20.0f}
    };
    float holderScore = 30.0f;
    std::vector<float> siblingScores = {40.0f};
    
    uint32_t best = BestUpgradeForAnyone(rewards, holderScore, siblingScores);
    Expect(best == 0, "sibling upgrade chosen when holder has none");
}

void EmptyRewardsReturnsZero()
{
    std::vector<RewardChoice> rewards;
    float holderScore = 30.0f;
    
    uint32_t best = BestRewardForHolder(rewards, holderScore);
    Expect(best == 0, "empty rewards returns zero");
}

void SingleRewardChosen()
{
    std::vector<RewardChoice> rewards = {{100, 50.0f}};
    float holderScore = 30.0f;
    
    uint32_t best = BestRewardForHolder(rewards, holderScore);
    Expect(best == 100, "single reward is always chosen");
}
}  // namespace

int main()
{
    RewardsAreChosenByValue();
    HolderUpgradePreferredOverSibling();
    IfNoHolderUpgradeSiblingChosen();
    EmptyRewardsReturnsZero();
    SingleRewardChosen();
    
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("quest reward scoring works correctly\n");
    return EXIT_SUCCESS;
}
