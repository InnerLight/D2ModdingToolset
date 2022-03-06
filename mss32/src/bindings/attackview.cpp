/*
 * This file is part of the modding toolset for Disciples 2.
 * (https://github.com/VladimirMakeev/D2ModdingToolset)
 * Copyright (C) 2022 Vladimir Makeev.
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

#include "attackview.h"
#include "attack.h"
#include <sol/sol.hpp>

namespace bindings {

AttackView::AttackView(const game::IAttack* attack)
    : attack{attack}
{ }

void AttackView::bind(sol::state& lua)
{
    auto attackView = lua.new_usertype<AttackView>("AttackView");
    attackView["type"] = sol::property(&AttackView::getAttackClass);
    attackView["source"] = sol::property(&AttackView::getAttackSource);
    attackView["initiative"] = sol::property(&AttackView::getInitiative);
    attackView["power"] = sol::property(&AttackView::getPower);
    attackView["reach"] = sol::property(&AttackView::getReach);
    attackView["damage"] = sol::property(&AttackView::getDamage);
    attackView["heal"] = sol::property(&AttackView::getHeal);
    attackView["infinite"] = sol::property(&AttackView::isInfinite);
    attackView["crit"] = sol::property(&AttackView::canCrit);
}

int AttackView::getAttackClass() const
{
    auto attackClass{attack->vftable->getAttackClass(attack)};
    if (!attackClass) {
        return 0;
    }

    return static_cast<int>(attackClass->id);
}

int AttackView::getAttackSource() const
{
    auto attackSource{attack->vftable->getAttackSource(attack)};
    if (!attackSource) {
        return 0;
    }

    return static_cast<int>(attackSource->id);
}

int AttackView::getInitiative() const
{
    return attack->vftable->getInitiative(attack);
}

int AttackView::getPower() const
{
    int power{};
    attack->vftable->getPower(attack, &power);

    return power;
}

int AttackView::getReach() const
{
    auto reach{attack->vftable->getAttackReach(attack)};
    if (!reach) {
        return 0;
    }

    return static_cast<int>(reach->id);
}

int AttackView::getDamage() const
{
    return attack->vftable->getQtyDamage(attack);
}

int AttackView::getHeal() const
{
    return attack->vftable->getQtyHeal(attack);
}

bool AttackView::isInfinite() const
{
    return attack->vftable->getInfinite(attack);
}

bool AttackView::canCrit() const
{
    return attack->vftable->getCritHit(attack);
}

} // namespace bindings