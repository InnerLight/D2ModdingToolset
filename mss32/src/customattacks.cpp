/*
 * This file is part of the modding toolset for Disciples 2.
 * (https://github.com/VladimirMakeev/D2ModdingToolset)
 * Copyright (C) 2021 Stanislav Egorov.
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

#include "customattacks.h"
#include "log.h"
#include "scripts.h"
#include "utils.h"
#include <fmt/format.h>
#include <sol/sol.hpp>

namespace hooks {

void readAttackSources(const sol::table& table, CustomAttackSources& value)
{
    auto sources = table.get<sol::optional<sol::table>>("sources");
    if (!sources.has_value())
        return;

    for (const auto& entry : sources.value()) {
        const auto& source = entry.second.as<sol::table>();
        value[source["id"]] = {source["textId"]};
    }
}

void initialize(CustomAttacks& value)
{
    const auto path{hooks::scriptsFolder() / "customattacks.lua"};

    try {
        sol::state lua;
        if (!loadScript(path, lua))
            return;

        const sol::table& table = lua["customAttacks"];
        readAttackSources(table, value.sources);
    } catch (const std::exception& e) {
        showErrorMessageBox(fmt::format("Failed to read script '{:s}'.\n"
                                        "Reason: '{:s}'",
                                        path.string(), e.what()));
    }
}

const CustomAttacks& customAttacks()
{
    static CustomAttacks value;
    static bool initialized = false;

    if (!initialized) {
        initialize(value);
        initialized = true;
    }

    return value;
}

} // namespace hooks