WorldPosition NewRpgBaseAction::SelectRandomGrindPos(Player* bot)
{
    std::vector<WorldLocation> const& locs = sTravelMgr.GetLocsPerLevelCache(bot->GetLevel());
    float hiRange = 500.0f;
    float loRange = 2500.0f;
    if (bot->GetLevel() < 5)
    {
        hiRange /= 3;
        loRange /= 3;
    }
    std::vector<WorldLocation> lo_prepared_locs, hi_prepared_locs;

    bool inCity = false;
    if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId()))
    {
        if (zone->flags & AREA_FLAG_CAPITAL)
            inCity = true;
    }

    // A bot standing in a zone that caches no grind spot (open sea, a strait)
    // has nothing to pick if it may only pick from its own zone, and the
    // caller then sits it down for good. The second pass lets it pick the
    // nearest spot in any zone of its map so it can swim to shore.
    for (int pass = 0; pass < 2 && lo_prepared_locs.empty(); ++pass)
    {
        bool const sameZoneOnly = !inCity && pass == 0;
        for (auto& loc : locs)
        {
            if (bot->GetMapId() != loc.GetMapId())
                continue;

            if (bot->GetExactDist(loc) > 2500.0f)
                continue;

            if (sameZoneOnly && bot->GetMap()->GetZoneId(bot->GetPhaseMask(), loc.GetPositionX(),
                                                         loc.GetPositionY(), loc.GetPositionZ()) != bot->GetZoneId())
                continue;

            if (bot->GetExactDist(loc) < hiRange)
            {
                hi_prepared_locs.push_back(loc);
            }

            if (bot->GetExactDist(loc) < loRange)
            {
                lo_prepared_locs.push_back(loc);
            }
        }
    }
    WorldPosition dest{};
    if (urand(1, 100) <= 50 && !hi_prepared_locs.empty())
    {
        uint32 idx = urand(0, hi_prepared_locs.size() - 1);
        dest = hi_prepared_locs[idx];
    }
    else if (!lo_prepared_locs.empty())
    {
        uint32 idx = urand(0, lo_prepared_locs.size() - 1);
        dest = lo_prepared_locs[idx];
    }
    LOG_DEBUG("playerbots", "[New RPG] Bot {} select random grind pos Map:{} X:{} Y:{} Z:{} ({}+{} available in {})",
              bot->GetName(), dest.GetMapId(), dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(),
              hi_prepared_locs.size(), lo_prepared_locs.size() - hi_prepared_locs.size(), locs.size());
    return dest;
}
