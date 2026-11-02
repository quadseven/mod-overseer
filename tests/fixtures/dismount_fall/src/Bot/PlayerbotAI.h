
class PlayerbotAI : public PlayerbotAIBase
{
    // Consumes queued chat commands from FindReaction(); needs HandleCommands().
    friend class ReactionEngine;

public:
    PlayerbotAI();
    PlayerbotAI(Player* bot);
    virtual ~PlayerbotAI();

    void UpdateAI(uint32 elapsed, bool minimal = false) override;
    void UpdateAIInternal(uint32 elapsed, bool minimal = false) override;
    bool UpdateAIReaction(uint32 elapsed, bool minimal, bool canControlSelf);
    Engine* GetCurrentEngine() const { return currentEngine; }
    void SetActionDuration(Action const* action);
    time_t allowActiveCheckTimer[MAX_ACTIVITY_TYPE];
    ReactionEngine* reactionEngine = nullptr;
    bool inCombat = false;
    BotCheatMask cheatMask = BotCheatMask::none;
    Position jumpDestination = Position();
    uint32 nextTransportCheck = 0;
    bool spellInterruptRequested = false;
};

#endif
