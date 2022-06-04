/*
 * This file is part of the modding toolset for Disciples 2.
 * (https://github.com/VladimirMakeev/D2ModdingToolset)
 * Copyright (C) 2021 Vladimir Makeev.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef USRACIALSOLDIER_H
#define USRACIALSOLDIER_H

#include "usglobal.h"
#include "ussoldierimpl.h"
#include "usunitextension.h"
#include "usunitimpl.h"
#include <cstddef>

namespace game {

struct IUsRacialSoldierVftable;

struct IUsRacialSoldier : public IUsUnitExtensionT<IUsRacialSoldierVftable>
{ };

struct IUsRacialSoldierVftable : public IUsUnitExtensionVftable
{
    using GetId = const CMidgardID*(__thiscall*)(const IUsRacialSoldier* thisptr);

    GetId getPrevUnitImplId;
    GetId getUpgradeBuildingId;
    GetId getEnrollBuildingId;
};

assert_vftable_size(IUsRacialSoldierVftable, 4);

/** Holds soldier specific data read from GUnits.dbf. */
struct TUsRacialSoldierData
{
    int xpNext;                   /**< XP_NEXT  */
    CMidgardID prevUnitImplId;    /**< PREV_ID */
    CMidgardID upgradeBuildingId; /**< UPGRADE_B */
    CMidgardID enrollBuildingId;  /**< ENROLL_B */
};

assert_size(TUsRacialSoldierData, 16);

/**
 * Represents soldier unit template.
 * Soldier unit in scenario is represented by CMidUnit
 * which accesses soldier template data via CMidUnit::unitImpl pointer.
 * Soldier units are hired in cities and can be upgraded depending on buildings in capital.
 * They are ordinary units in groups.
 */
struct TUsRacialSoldier
    : public IUsRacialSoldier
    , public IUsGlobal
    , public TUsUnitImpl
    , public TUsSoldierImpl
{
    TUsRacialSoldierData* data;
};

assert_size(TUsRacialSoldier, 40);
assert_offset(TUsRacialSoldier, TUsRacialSoldier::IUsRacialSoldier::vftable, 0);
assert_offset(TUsRacialSoldier, TUsRacialSoldier::IUsGlobal::vftable, 4);
assert_offset(TUsRacialSoldier, TUsRacialSoldier::TUsUnitImpl::vftable, 8);
assert_offset(TUsRacialSoldier, TUsRacialSoldier::TUsSoldierImpl::vftable, 28);
assert_offset(TUsRacialSoldier, data, 36);

} // namespace game

#endif // USRACIALSOLDIER_H
