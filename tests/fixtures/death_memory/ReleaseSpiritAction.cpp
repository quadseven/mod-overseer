void ReleaseSpiritAction::IncrementDeathCount() const
{
    // BG deaths don't count, matching PlayerbotAI::DoNextAction.
    if (bot->InBattleground())
        return;

    // Death Count to prevent skeleton piles
    Player* master = botAI->GetMaster();
    if (!master || GET_PLAYERBOT_AI(master))
    {
        uint32 deathCount = AI_VALUE(uint32, "death count");
        context->GetValue<uint32>("death count")->Set(deathCount + 1);
    }
}
