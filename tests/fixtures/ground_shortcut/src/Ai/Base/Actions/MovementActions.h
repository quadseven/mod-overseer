class MovementAction : public Action
{
public:
    MovementAction(PlayerbotAI* botAI, std::string const name);

protected:
    bool JumpTo(uint32 mapId, float x, float y, float z, MovementPriority priority = MovementPriority::MOVEMENT_NORMAL);
    bool MoveNear(uint32 mapId, float x, float y, float z, float distance = sPlayerbotAIConfig.ContactDistance,
                  MovementPriority priority = MovementPriority::MOVEMENT_NORMAL);
    bool MoveToLOS(WorldObject* target, bool ranged = false);
    bool MoveTo(uint32 mapId, float x, float y, float z, bool idle = false, bool react = false,
                bool normal_only = false, bool exact_waypoint = false,
                MovementPriority priority = MovementPriority::MOVEMENT_NORMAL, bool lessDelay = false,
                bool backwards = false);
    bool MoveTo(WorldObject* target, float distance = 0.0f,
                MovementPriority priority = MovementPriority::MOVEMENT_NORMAL);
    bool MoveNear(WorldObject* target, float distance = sPlayerbotAIConfig.ContactDistance,
                  MovementPriority priority = MovementPriority::MOVEMENT_NORMAL);
    float GetFollowAngle();
    bool Follow(Unit* target, float distance = sPlayerbotAIConfig.FollowDistance);
    bool Follow(Unit* target, float distance, float angle);
    bool ChaseTo(WorldObject* obj, float distance = 0.0f);
    bool ReachCombatTo(Unit* target, float distance = 0.0f);
    float MoveDelay(float distance, bool backwards = false);
    void WaitForReach(float distance);
    void SetNextMovementDelay(float delayMillis);
    bool IsMovingAllowed(WorldObject* target);
    bool IsDuplicateMove(float x, float y, float z);
    bool IsWaitingForLastMove(MovementPriority priority);
    bool IsMovingAllowed();
    bool Flee(Unit* target);
    void ClearIdleState();
    void UpdateMovementState();
    bool MoveAway(Unit* target, float distance = sPlayerbotAIConfig.FleeDistance, bool backwards = false);
    bool MoveFromGroup(float distance);
    bool Move(float angle, float distance);
    bool MoveInside(uint32 mapId, float x, float y, float z, float distance = sPlayerbotAIConfig.FollowDistance,
                    MovementPriority priority = MovementPriority::MOVEMENT_NORMAL);
    void CreateWp(Player* wpOwner, float x, float y, float z, float o, uint32 entry, bool important = false);
    Position BestPositionForMeleeToFlee(Position pos, float radius);
    Position BestPositionForRangedToFlee(Position pos, float radius);
    bool FleePosition(Position pos, float radius, uint32 minInterval = 1000);
    bool CheckLastFlee(float curAngle, std::list<FleeInfo>& infoList);

protected:
    struct CheckAngle
    {
        float angle;
        bool strict;
    };

private:
    // float SearchBestGroundZForPath(float x, float y, float z, bool generatePath, float range = 20.0f, bool
    // normal_only = false, float step = 8.0f);
    const Movement::PointsArray SearchForBestPath(float x, float y, float z, float& modified_z, int maxSearchCount = 5,
                                                  bool normal_only = false, float step = 8.0f);
    bool wasMovementRestricted = false;
    void DoMovePoint(Unit* unit, float x, float y, float z, bool generatePath, bool backwards);
};

class FleeAction : public MovementAction
{
public:
    FleeAction(PlayerbotAI* botAI) : MovementAction(botAI, "flee")
    {
