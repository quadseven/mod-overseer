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

Unit* UnitManualSetValue::Get()
{
    // Prevent crashing by InWorld check
    if (value && value->IsInWorld())
        return value;
    return nullptr;
}
