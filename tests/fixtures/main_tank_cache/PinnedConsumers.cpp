// Source: playerbots 40884e9c and core 8a4e76ea. Runtime bodies, unchanged.

Unit* UnitCalculatedValue::Get()
{
    if (checkInterval < 2)
    {
        PerfMonitorOperation* pmo = sPerfMonitor.start(
            PERF_MON_VALUE, this->getName(), this->context ? &this->context->performanceStack : nullptr);
        value = Calculate();
        if (pmo)
            pmo->finish();
    }
    else
    {
        time_t now = getMSTime();
        if (!lastCheckTime || now - lastCheckTime >= checkInterval)
        {
            lastCheckTime = now;
            PerfMonitorOperation* pmo = sPerfMonitor.start(
                PERF_MON_VALUE, this->getName(), this->context ? &this->context->performanceStack : nullptr);
            value = Calculate();
            if (pmo)
                pmo->finish();
        }
    }
    // Prevent crashing by InWorld check
    if (value && value->IsInWorld())
        return value;
    return nullptr;
}

bool LowTankThreatTrigger::IsActive()
{
    Unit* current_target = AI_VALUE(Unit*, "current target");
    if (!current_target || current_target->IsPlayer())
        return false;  // players have no threat: Tricks of the Trade / Misdirection would be wasted

    Unit* mainTank = AI_VALUE(Unit*, "main tank");
    if (!mainTank)
        return false;

    ThreatManager& mgr = current_target->GetThreatMgr();
    float threat = mgr.GetThreat(bot);
    float tankThreat = mgr.GetThreat(mainTank);
    return tankThreat == 0.0f || threat > tankThreat * 0.5f;
}

Unit* PlayerbotAI::GetUnit(ObjectGuid guid)
{
    if (!guid)
        return nullptr;

    return ObjectAccessor::GetUnit(*bot, guid);
}

float ThreatManager::GetThreat(Unit const* who, bool includeOffline) const
{
    auto it = _myThreatListEntries.find(who->GetGUID());
    if (it == _myThreatListEntries.end())
        return 0.0f;
    return (includeOffline || it->second->IsAvailable()) ? it->second->GetThreat() : 0.0f;
}
