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

#ifndef CAPITALDATAHOOKS_H
#define CAPITALDATAHOOKS_H

namespace game {
struct CapitalData;
}

namespace hooks {

game::CapitalData** __fastcall allocateCapitalDataHooked(game::CapitalData** thisptr, int /*%edx*/);

void __stdcall readCapitalDataHooked(const char* imagesFolderPath, const char* filename);

} // namespace hooks

#endif // CAPITALDATAHOOKS_H