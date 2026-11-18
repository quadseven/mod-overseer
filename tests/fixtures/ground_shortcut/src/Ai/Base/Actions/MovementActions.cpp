
    if (dest.isSet())
        return MoveTo(dest.mapId, dest.x, dest.y, dest.z);
    else
        botAI->TellError("All paths not in LOS");

    return false;
}

bool MovementAction::MoveTo(uint32 mapId, float x, float y, float z, bool /*idle*/, bool /*react*/, bool normal_only,
                            bool exact_waypoint, MovementPriority priority, bool lessDelay, bool backwards)
{
    UpdateMovementState();
    if (!IsMovingAllowed())
    {
        return false;
    }
    if (IsDuplicateMove(x, y, z))
    {
        return false;
    }
    if (IsWaitingForLastMove(priority))
    {
        return false;
    }

    bool generatePath = !bot->IsFlying() && !bot->isSwimming();
    bool disableMoveSplinePath =
        sPlayerbotAIConfig.DisableMoveSplinePath >= 2 ||
        (sPlayerbotAIConfig.DisableMoveSplinePath == 1 && bot->InBattleground());
    if (Vehicle* vehicle = bot->GetVehicle())
    {
        VehicleSeatEntry const* seat = vehicle->GetSeatForPassenger(bot);
        Unit* vehicleBase = vehicle->GetBase();
        generatePath = !vehicleBase || !vehicleBase->CanFly();
        if (!vehicleBase || !seat || !seat->CanControl())  // is passenger and cant move anyway
            return false;

        float distance = vehicleBase->GetExactDist(x, y, z);  // use vehicle distance, not bot
        if (distance > 0.01f)
        {
            DoMovePoint(vehicleBase, x, y, z, generatePath, backwards);
            float speed = backwards ? vehicleBase->GetSpeed(MOVE_RUN_BACK) : vehicleBase->GetSpeed(MOVE_RUN);
            float delay = IN_MILLISECONDS * (distance / speed);
            if (lessDelay)
            {
                delay -= botAI->GetReactDelay();
            }
            delay = std::max(.0f, delay);
            delay = std::min((float)sPlayerbotAIConfig.MaxWaitForMove, delay);
            AI_VALUE(LastMovement&, "last movement").Set(mapId, x, y, z, bot->GetOrientation(), delay, priority);
            return true;
        }
    }
    else if (exact_waypoint || disableMoveSplinePath || !generatePath)
    {
        float distance = bot->GetExactDist(x, y, z);
        if (distance > 0.01f)
        {
            if (bot->IsSitState())
                bot->SetStandState(UNIT_STAND_STATE_STAND);

            // bot->CastStop();

            DoMovePoint(bot, x, y, z, generatePath, backwards);
            float delay = IN_MILLISECONDS * MoveDelay(distance, backwards);
            if (lessDelay)
            {
                delay -= botAI->GetReactDelay();
            }
            delay = std::max(.0f, delay);
            delay = std::min((float)sPlayerbotAIConfig.MaxWaitForMove, delay);
            AI_VALUE(LastMovement&, "last movement").Set(mapId, x, y, z, bot->GetOrientation(), delay, priority);
            return true;
        }
    }
    else
    {
        float modifiedZ;
        Movement::PointsArray path =
            SearchForBestPath(x, y, z, modifiedZ, sPlayerbotAIConfig.MaxMovementSearchTime, normal_only);
        if (modifiedZ == INVALID_HEIGHT)
            return false;
        float distance = bot->GetExactDist(x, y, modifiedZ);
        if (distance > 0.01f)
        {
            if (bot->IsSitState())
                bot->SetStandState(UNIT_STAND_STATE_STAND);

            // bot->CastStop();

            DoMovePoint(bot, x, y, modifiedZ, generatePath, backwards);
            float delay = IN_MILLISECONDS * MoveDelay(distance, backwards);
            if (lessDelay)
            {
                delay -= botAI->GetReactDelay();
            }
            delay = std::max(.0f, delay);
            delay = std::min((float)sPlayerbotAIConfig.MaxWaitForMove, delay);
            AI_VALUE(LastMovement&, "last movement")
                .Set(mapId, x, y, modifiedZ, bot->GetOrientation(), delay, priority);
            return true;
        }
    }

    return false;
    //
    // // LOG_DEBUG("playerbots", "IsMovingAllowed {}", IsMovingAllowed());
    // bot->AddUnitMovementFlag()

    // bool isVehicle = false;
    // Unit* mover = bot;
    // if (Vehicle* vehicle = bot->GetVehicle())
    // {
    //     VehicleSeatEntry const* seat = vehicle->GetSeatForPassenger(bot);
    //     LOG_DEBUG("playerbots", "!seat || !seat->CanControl() {}", !seat || !seat->CanControl());
    //     if (!seat || !seat->CanControl())
    //         return false;

    //     isVehicle = true;
    //     mover = vehicle->GetBase();
    // }

    // bool detailedMove = botAI->AllowActivity(DETAILED_MOVE_ACTIVITY);
    // if (!detailedMove)
    // {
    //     time_t now = time(nullptr);
    //     if (AI_VALUE(LastMovement&, "last movement").nextTeleport > now) // We can not teleport yet. Wait.
    //     {
    //         LOG_DEBUG("playerbots", "AI_VALUE(LastMovement&, \"last movement\").nextTeleport > now");
    //         botAI->SetNextCheckDelay(
    //             (AI_VALUE(LastMovement&, "last movement").nextTeleport - now) * IN_MILLISECONDS);
    //         return true;
    //     }
    // }

    // float minDist = sPlayerbotAIConfig.TargetPosRecalcDistance; //Minium distance a bot should move.
    // float maxDist = sPlayerbotAIConfig.ReactDistance;           //Maxium distance a bot can move in one single
    // action. float originalZ = z;                                        // save original destination height to check
    // if bot needs to fly up

    // bool generatePath = !bot->IsFlying() && !bot->HasUnitMovementFlag(MOVEMENTFLAG_SWIMMING) && !bot->IsInWater() &&
    // !bot->IsUnderWater(); if (generatePath)
    // {
    //     z += CONTACT_DISTANCE;
    //     mover->UpdateAllowedPositionZ(x, y, z);
    // }

    // if (!isVehicle && !IsMovingAllowed() && bot->isDead())
    // {
    //     bot->StopMoving();
    //     LOG_DEBUG("playerbots", "!isVehicle && !IsMovingAllowed() && bot->isDead()");
    //     return false;
    // }

    // if (!isVehicle && bot->isMoving() && !IsMovingAllowed())
    // {
    //     if (!bot->HasUnitState(UNIT_STATE_IN_FLIGHT))
    //         bot->StopMoving();
    //     LOG_DEBUG("playerbots", "!isVehicle && bot->isMoving() && !IsMovingAllowed()");
    //     return false;
    // }

    // LastMovement& lastMove = *context->GetValue<LastMovement&>("last movement");

    // WorldPosition startPosition = WorldPosition(bot);             //Current location of the bot
    // WorldPosition endPosition = WorldPosition(mapId, x, y, z, 0); //The requested end location
    // WorldPosition movePosition;                                   //The actual end location

    // float totalDistance = startPosition.distance(endPosition);    //Total distance to where we want to go
    // float maxDistChange = totalDistance * 0.1;                    //Maximum change between previous destination
    // before needing a recalulation

    // if (totalDistance < minDist)
    // {
    //     if (lastMove.lastMoveShort.distance(endPosition) < maxDistChange)
    //         AI_VALUE(LastMovement&, "last movement").clear();

    //     mover->StopMoving();
    //     LOG_DEBUG("playerbots", "totalDistance < minDist");
    //     return false;
    // }

    // TravelPath movePath;

    // if (lastMove.lastMoveShort.distance(endPosition) < maxDistChange &&
    // startPosition.distance(lastMove.lastMoveShort) < maxDist) //The last short movement was to the same place we want
    // to move now.
    //     movePosition = endPosition;
    // else if (!lastMove.lastPath.empty() && lastMove.lastPath.getBack().distance(endPosition) < maxDistChange) //The
    // last long movement was to the same place we want to move now.
    // {
    //     movePath = lastMove.lastPath;
    // }
    // else
    // {
    //     movePosition = endPosition;

    //     if (startPosition.GetMapId() != endPosition.GetMapId() || totalDistance > maxDist)
    //     {
    //         if (!TravelNodeMap::instance().getNodes().empty() && !bot->InBattleground())
    //         {
    //             if (sPlayerbotAIConfig.TweakValue)
    //             {
    //                 if (lastMove.future.valid())
    //                 {
    //                     movePath = lastMove.future.get();
    //                 }
    //                 else
    //                 {
    //                     lastMove.future = std::async(&TravelNodeMap::getFullPath, startPosition, endPosition, bot);
    //                     LOG_DEBUG("playerbots", "lastMove.future = std::async(&TravelNodeMap::getFullPath,
    //                     startPosition, endPosition, bot);"); return true;
    //                 }
    //             }
    //             else
    //                 movePath = TravelNodeMap::instance().getFullPath(startPosition, endPosition, bot);

    //             if (movePath.empty())
    //             {
    //                 //We have no path. Beyond 450yd the standard PathGenerator will probably move the wrong way.
    //                 if (ServerFacade::instance().IsDistanceGreaterThan(totalDistance, maxDist * 3))
    //                 {
    //                     movePath.clear();
    //                     movePath.addPoint(endPosition);
    //                     AI_VALUE(LastMovement&, "last movement").setPath(movePath);

    //                     bot->StopMoving();
    //                     if (botAI->HasStrategy("debug move", BOT_STATE_NON_COMBAT))
    //                         botAI->TellMasterNoFacing("I have no path");
    //                     LOG_DEBUG("playerbots", "ServerFacade::instance().IsDistanceGreaterThan(totalDistance, maxDist * 3)");
    //                     return false;
    //                 }

    //                 movePosition = endPosition;
    //             }
    //         }
    //         else
    //         {
    //             //Use standard PathGenerator to find a route.
    //             movePosition = endPosition;
    //         }
    //     }
    // }

    // if (movePath.empty() && movePosition.distance(startPosition) > maxDist)
    // {
    //     //Use standard PathGenerator to find a route.
    //     PathGenerator path(mover);
    //     path.CalculatePath(movePosition.GetPositionX(), movePosition.GetPositionY(), movePosition.GetPositionZ(), false);
    //     PathType type = path.GetPathType();
    //     Movement::PointsArray const& points = path.GetPath();
    //     movePath.addPath(startPosition.fromPointsArray(points));
    // }

    // if (!movePath.empty())
    // {
    //     if (movePath.makeShortCut(startPosition, maxDist))
    //         if (botAI->HasStrategy("debug move", BOT_STATE_NON_COMBAT))
    //             botAI->TellMasterNoFacing("Found a shortcut.");

    //     if (movePath.empty())
    //     {
    //         AI_VALUE(LastMovement&, "last movement").setPath(movePath);

    //         if (botAI->HasStrategy("debug move", BOT_STATE_NON_COMBAT))
    //             botAI->TellMasterNoFacing("Too far from path. Rebuilding.");
    //         LOG_DEBUG("playerbots", "movePath.empty()");
    //         return true;
    //     }

    //     TravelNodePathType pathType;
    //     uint32 entry;
    //     movePosition = movePath.getNextPoint(startPosition, maxDist, pathType, entry);

    //     if (pathType == TravelNodePathType::portal) // && !botAI->isRealPlayer())
    //     {
    //         //Log bot movement
    //         if (sPlayerbotAIConfig.HasLog("bot_movement.csv"))
    //         {
    //             WorldPosition telePos;
    //             if (entry)
    //             {
    //                 if (AreaTriggerTeleport const* at = sObjectMgr->GetAreaTriggerTeleport(entry))
    //                     telePos = WorldPosition(at->target_mapId, at->target_X, at->target_Y, at->target_Z,
    //                     at->target_Orientation);
    //             }
    //             else
    //                 telePos = movePosition;

    //             std::ostringstream out;
    //             out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
    //             out << bot->GetName() << ",";
    //             if (telePos && telePos.GetExactDist(movePosition) > 0.001)
    //                 startPosition.printWKT({ startPosition, movePosition, telePos }, out, 1);
    //             else
    //                 startPosition.printWKT({ startPosition, movePosition }, out, 1);

    //             out << std::to_string(bot->getRace()) << ",";
    //             out << std::to_string(bot->getClass()) << ",";
    //             out << bot->GetLevel() << ",";
    //             out << (entry ? -1 : entry);

    //             sPlayerbotAIConfig.Log("bot_movement.csv", out.str().c_str());
    //         }

    //         if (entry)
    //         {
    //             AI_VALUE(LastMovement&, "last area trigger").lastAreaTrigger = entry;
    //         }
    //         else
    //         {
    //             LOG_DEBUG("playerbots", "!entry");
    //             return bot->TeleportTo(movePosition.GetMapId(), movePosition.GetPositionX(), movePosition.GetPositionY(),
    //             movePosition.GetPositionZ(), movePosition.GetOrientation(), 0);
    //         }
    //     }

    //     if (pathType == TravelNodePathType::transport && entry)
    //     {
    //         if (!bot->GetTransport())
    //         {
    //             for (auto& transport : movePosition.getTransports(entry))
    //                 if (movePosition.sqDistance2d(WorldPosition((WorldObject*)transport)) < 5 * 5)
    //                     transport->AddPassenger(bot, true);
    //         }
    //         WaitForReach(100.0f);
    //         LOG_DEBUG("playerbots", "pathType == TravelNodePathType::transport && entry");
    //         return true;
    //     }

    //     if (pathType == TravelNodePathType::flightPath && entry)
    //     {
    //         if (TaxiPathEntry const* tEntry = sTaxiPathStore.LookupEntry(entry))
    //         {
    //             Creature* unit = nullptr;

    //             if (!bot->m_taxi.IsTaximaskNodeKnown(tEntry->from))
    //             {
    //                 GuidVector npcs = AI_VALUE(GuidVector, "nearest npcs");
    //                 for (GuidVector::iterator i = npcs.begin(); i != npcs.end(); i++)
    //                 {
    //                     Creature* unit = bot->GetNPCIfCanInteractWith(*i, UNIT_NPC_FLAG_FLIGHTMASTER);
    //                     if (!unit)
    //                         continue;

    //                     bot->GetSession()->SendLearnNewTaxiNode(unit);

    //                     unit->SetFacingTo(unit->GetAngle(bot));
    //                 }
    //             }

    //             uint32 botMoney = bot->GetMoney();
    //             if (botAI->HasCheat(BotCheatMask::gold))
    //             {
    //                 bot->SetMoney(10000000);
    //             }

    //             bool goTaxi = bot->ActivateTaxiPathTo({ tEntry->from, tEntry->to }, unit, 1);

    //             if (botAI->HasCheat(BotCheatMask::gold))
    //                 bot->SetMoney(botMoney);
    //             LOG_DEBUG("playerbots", "goTaxi");
    //             return goTaxi;
    //         }
    //     }

    //     // if (pathType == TravelNodePathType::teleportSpell && entry)
    //     // {
    //     //     if (entry == 8690)
    //     //     {
    //     //         if (!bot->HasSpellCooldown(8690))
    //     //         {
    //     //             return botAI->DoSpecificAction("hearthstone", Event("move action"));
    //     //         }
    //     //         else
    //     //         {
    //     //             movePath.clear();
    //     //             AI_VALUE(LastMovement&, "last movement").setPath(movePath);
    //     //             LOG_DEBUG("playerbots", "bot->HasSpellCooldown(8690)");
    //     //             return false;
    //     //         }
    //     //     }
    //     // }

    //     //if (!isTransport && bot->GetTransport())
    //     //    bot->GetTransport()->RemovePassenger(bot);
    // }

    // AI_VALUE(LastMovement&, "last movement").setPath(movePath);

    // if (!movePosition || movePosition.GetMapId() != bot->GetMapId())
    // {
    //     movePath.clear();
    //     AI_VALUE(LastMovement&, "last movement").setPath(movePath);

    //     if (botAI->HasStrategy("debug move", BOT_STATE_NON_COMBAT))
    //         botAI->TellMasterNoFacing("No point. Rebuilding.");
    //     LOG_DEBUG("playerbots", "!movePosition || movePosition.GetMapId() != bot->GetMapId()");
    //     return false;
    // }

    // if (movePosition.distance(startPosition) > maxDist)
    // {
    //     //Use standard pathfinder to find a route.
    //     PathGenerator path(mover);
    //     path.CalculatePath(movePosition.getX(), movePosition.getY(), movePosition.getZ(), false);
    //     PathType type = path.GetPathType();
    //     Movement::PointsArray const& points = path.GetPath();
    //     movePath.addPath(startPosition.fromPointsArray(points));
    //     TravelNodePathType pathType;
    //     uint32 entry;
    //     movePosition = movePath.getNextPoint(startPosition, maxDist, pathType, entry);
    // }

    // if (movePosition == WorldPosition())
    // {
    //     movePath.clear();

    //     AI_VALUE(LastMovement&, "last movement").setPath(movePath);

    //     if (botAI->HasStrategy("debug move", BOT_STATE_NON_COMBAT))
    //         botAI->TellMasterNoFacing("No point. Rebuilding.");

    //     return false;
    // }

    // //Visual waypoints
    // if (botAI->HasStrategy("debug move", BOT_STATE_NON_COMBAT))
    // {
    //     if (!movePath.empty())
    //     {
    //         float cx = x;
    //         float cy = y;
    //         float cz = z;
    //         for (auto i : movePath.getPath())
    //         {
    //             CreateWp(bot, i.point.GetPositionX(), i.point.GetPositionY(), i.point.GetPositionZ(), 0.f, 2334);

    //             cx = i.point.GetPositionX();
    //             cy = i.point.GetPositionY();
    //             cz = i.point.GetPositionZ();
    //         }
    //     }
    //     else
    //         CreateWp(bot, movePosition.GetPositionX(), movePosition.GetPositionY(), movePosition.GetPositionZ(), 0, 2334, true);
    // }

    // //Log bot movement
    // if (sPlayerbotAIConfig.HasLog("bot_movement.csv") && lastMove.lastMoveShort.GetExactDist(movePosition) > 0.001)
    // {
    //     std::ostringstream out;
    //     out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
    //     out << bot->GetName() << ",";
    //     startPosition.printWKT({ startPosition, movePosition }, out, 1);
    //     out << std::to_string(bot->getRace()) << ",";
    //     out << std::to_string(bot->getClass()) << ",";
    //     out << bot->GetLevel();
    //     out << 0;

    //     sPlayerbotAIConfig.Log("bot_movement.csv", out.str().c_str());
    // }
    // // LOG_DEBUG("playerbots", "({}, {}) -> ({}, {})", startPosition.GetPositionX(), startPosition.GetPositionY(),
    // movePosition.GetPositionX(), movePosition.GetPositionY()); if (!react)
    //     if (totalDistance > maxDist)
    //         WaitForReach(startPosition.distance(movePosition) - 10.0f);
    //     else
    //         WaitForReach(startPosition.distance(movePosition));

    // if (!isVehicle)
    // {
    //     bot->HandleEmoteCommand(0);
    //     if (bot->IsSitState())
    //         bot->SetStandState(UNIT_STAND_STATE_STAND);

    //     bot->CastStop();

    //  /* Why do we do this?
    // if (lastMove.lastMoveShort.distance(movePosition) < minDist)
    // {
    //     bot->StopMoving();
    //     bot->GetMotionMaster()->Clear();
    // }
    // */

    // // Clean movement if not already moving the same way.
    // // if (mover->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
    // // {
    // //     mover->StopMoving();
    // //     mover->GetMotionMaster()->Clear();
    // // }
    // // else
    // // {
    // //     mover->GetMotionMaster()->GetDestination(x, y, z);
    // //     if (movePosition.distance(WorldPosition(movePosition.GetMapId(), x, y, z, 0)) > minDist)
    // //     {
    // //         mover->StopMoving();
    // //         mover->GetMotionMaster()->Clear();
    // //     }
    // // }

    // if (totalDistance > maxDist && !detailedMove && !botAI->HasPlayerNearby(&movePosition)) // Why walk if you can
    // fly?
    // {
    //     time_t now = time(nullptr);

    //     AI_VALUE(LastMovement&, "last movement").nextTeleport = now +
    //     (time_t)MoveDelay(startPosition.distance(movePosition)); LOG_DEBUG("playerbots", "totalDistance > maxDist &&
    //     !detailedMove && !botAI->HasPlayerNearby(&movePosition)"); return bot->TeleportTo(movePosition.GetMapId(),
    //     movePosition.GetPositionX(), movePosition.GetPositionY(), movePosition.GetPositionZ(), startPosition.getAngleTo(movePosition));
    // }

    // // walk if master walks and is close
    // bool masterWalking = false;
    // if (botAI->GetMaster())
    // {
    //     if (botAI->GetMaster()->m_movementInfo.HasMovementFlag(MOVEMENTFLAG_WALKING) &&
    //     ServerFacade::instance().GetDistance2d(bot, botAI->GetMaster()) < 20.0f)
    //         masterWalking = true;
    // }

    // if (masterWalking)
    //     bot->SetWalk(true);

    // bot->SendMovementFlagUpdate();
    // // LOG_DEBUG("playerbots", "normal move? {} {} {}",
    // !bot->HasAuraType(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED) && !bot->HasAuraType(SPELL_AURA_FLY),
    // //     bot->HasUnitFlag(UNIT_FLAG_DISABLE_MOVE), bot->getStandState());
    // if (!bot->HasAuraType(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED) && !bot->HasAuraType(SPELL_AURA_FLY))
    // {
    //     bot->SetWalk(masterWalking);
    //     bot->GetMotionMaster()->MovePoint(movePosition.GetMapId(), movePosition.GetPositionX(), movePosition.GetPositionY(),
    //     movePosition.GetPositionZ(), generatePath); WaitForReach(startPosition.distance(movePosition));
    //     // LOG_DEBUG("playerbots", "Movepoint to ({}, {})", movePosition.GetPositionX(), movePosition.GetPositionY());
    // }
    // else
    // {
    //     bool needFly = false;
    //     bool needLand = false;
    //     bool isFly = bot->IsFlying();

    //     if (!isFly && originalZ > bot->GetPositionZ() && (originalZ - bot->GetPositionZ()) > 5.0f)
    //         needFly = true;

    //     if (needFly && !isFly)
    //     {
    //         WorldPacket data(SMSG_SPLINE_MOVE_SET_FLYING, 9);
    //         data << bot->GetPackGUID();
    //         bot->SendMessageToSet(&data, true);

    //         if (!bot->m_movementInfo.HasMovementFlag(MOVEMENTFLAG_FLYING))
    //             bot->m_movementInfo.AddMovementFlag(MOVEMENTFLAG_FLYING);

    //         if (!bot->m_movementInfo.HasMovementFlag(MOVEMENTFLAG_DISABLE_GRAVITY))
    //             bot->m_movementInfo.AddMovementFlag(MOVEMENTFLAG_DISABLE_GRAVITY);
    //     }

    //     if (isFly)
    //     {
    //         float ground = bot->GetPositionZ();
    //         float height = bot->GetMap()->GetWaterOrGroundLevel(bot->GetPositionX(), bot->GetPositionY(),
    //         bot->GetPositionZ(), ground); if (bot->GetPositionZ() > originalZ && (bot->GetPositionZ() - originalZ
    //         < 5.0f) && (fabs(originalZ - ground) < 5.0f))
    //             needLand = true;

    //         if (needLand)
    //         {
    //             WorldPacket data(SMSG_SPLINE_MOVE_UNSET_FLYING, 9);
    //             data << bot->GetPackGUID();
    //             bot->SendMessageToSet(&data, true);

    //             if (bot->m_movementInfo.HasMovementFlag(MOVEMENTFLAG_FLYING))
    //                 bot->m_movementInfo.RemoveMovementFlag(MOVEMENTFLAG_FLYING);

    //             if (bot->m_movementInfo.HasMovementFlag(MOVEMENTFLAG_DISABLE_GRAVITY))
    //                 bot->m_movementInfo.RemoveMovementFlag(MOVEMENTFLAG_DISABLE_GRAVITY);
    //         }
    //     }

    //     bot->GetMotionMaster()->MovePoint(movePosition.GetMapId(), Position(movePosition.GetPositionX(), movePosition.GetPositionY(),
    //     movePosition.GetPositionZ(), 0.f)); WaitForReach(startPosition.distance(movePosition)); LOG_DEBUG("playerbots",
    //     "Movepoint to ({}, {})", movePosition.GetPositionX(), movePosition.GetPositionY());
    // }

    // AI_VALUE(LastMovement&, "last movement").setShort(movePosition);

    // if (!idle)
    //     ClearIdleState();

    // LOG_DEBUG("playerbots", "return true in the end");
    // return true;
}
