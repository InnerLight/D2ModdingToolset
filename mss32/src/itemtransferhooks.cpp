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

#include "itemtransferhooks.h"
#include "button.h"
#include "citystackinterf.h"
#include "dynamiccast.h"
#include "exchangeinterf.h"
#include "fortification.h"
#include "functor.h"
#include "globaldata.h"
#include "itembase.h"
#include "itemcategory.h"
#include "log.h"
#include "midbag.h"
#include "midgardobjectmap.h"
#include "miditem.h"
#include "midstack.h"
#include "netmessages.h"
#include "phasegame.h"
#include "pickupdropinterf.h"
#include "utils.h"
#include "visitors.h"
#include <fmt/format.h>
#include <optional>
#include <vector>

namespace hooks {

using ItemFilter = std::function<bool(game::IMidgardObjectMap* objectMap,
                                      const game::CMidgardID* itemId)>;

static const game::LItemCategory* getItemCategoryById(game::IMidgardObjectMap* objectMap,
                                                      const game::CMidgardID* itemId)
{
    using namespace game;

    auto item = static_cast<CMidItem*>(
        objectMap->vftable->findScenarioObjectById(objectMap, itemId));
    if (!item) {
        logError("mssProxyError.log", fmt::format("Could not find item {:s}", idToString(itemId)));
        return nullptr;
    }

    auto global = GlobalDataApi::get();
    auto globalData = *global.getGlobalData();

    auto globalItem = global.findItemById(globalData->itemTypes, &item->globalItemId);
    if (!globalItem) {
        logError("mssProxyError.log",
                 fmt::format("Could not find global item {:s}", idToString(&item->globalItemId)));
        return nullptr;
    }

    return globalItem->vftable->getCategory(globalItem);
}

static bool isPotion(game::IMidgardObjectMap* objectMap, const game::CMidgardID* itemId)
{
    using namespace game;

    auto category = getItemCategoryById(objectMap, itemId);
    if (!category) {
        return false;
    }

    const auto& categories = ItemCategories::get();
    const auto id = category->id;

    return categories.potionBoost->id == id || categories.potionHeal->id == id
           || categories.potionPermanent->id == id || categories.potionRevive->id == id;
}

static bool isSpell(game::IMidgardObjectMap* objectMap, const game::CMidgardID* itemId)
{
    using namespace game;

    auto category = getItemCategoryById(objectMap, itemId);
    if (!category) {
        return false;
    }

    const auto& categories = ItemCategories::get();
    const auto id = category->id;

    return categories.scroll->id == id || categories.wand->id == id;
}

/** Transfers items from src object to dst. */
static void transferItems(const std::vector<game::CMidgardID>& items,
                          game::CPhaseGame* phaseGame,
                          const game::CMidgardID* dstObjectId,
                          const char* dstObjectName,
                          const game::CMidgardID* srcObjectId,
                          const char* srcObjectName)
{
    using namespace game;

    auto objectMap = CPhaseApi::get().getObjectMap(&phaseGame->phase);
    const auto& exchangeItem = VisitorApi::get().exchangeItem;
    const auto& sendExchangeItemMsg = NetMessagesApi::get().sendStackExchangeItemMsg;

    for (const auto& item : items) {
        sendExchangeItemMsg(phaseGame, srcObjectId, dstObjectId, &item, 1);

        if (!exchangeItem(srcObjectId, dstObjectId, &item, objectMap, 1)) {
            logError("mssProxyError.log",
                     fmt::format("Failed to transfer item {:s} from {:s} {:s} to {:s} {:s}",
                                 idToString(&item), srcObjectName, idToString(srcObjectId),
                                 dstObjectName, idToString(dstObjectId)));
        }
    }
}

/** Transfers city items to visiting stack. */
static void transferCityToStack(game::CPhaseGame* phaseGame,
                                const game::CMidgardID* cityId,
                                std::optional<ItemFilter> itemFilter = std::nullopt)
{
    using namespace game;

    auto objectMap = CPhaseApi::get().getObjectMap(&phaseGame->phase);
    auto obj = objectMap->vftable->findScenarioObjectById(objectMap, cityId);
    if (!obj) {
        logError("mssProxyError.log", fmt::format("Could not find city {:s}", idToString(cityId)));
        return;
    }

    auto fortification = static_cast<CFortification*>(obj);
    if (fortification->stackId == emptyId) {
        return;
    }

    auto& inventory = fortification->inventory;
    std::vector<CMidgardID> items;
    const int itemsTotal = inventory.vftable->getItemsCount(&inventory);
    for (int i = 0; i < itemsTotal; ++i) {
        auto item = inventory.vftable->getItem(&inventory, i);
        if (!itemFilter || (itemFilter && (*itemFilter)(objectMap, item))) {
            items.push_back(*item);
        }
    }

    transferItems(items, phaseGame, &fortification->stackId, "stack", cityId, "city");
}

void __fastcall cityInterfTransferAllToStack(game::CCityStackInterf* thisptr, int /*%edx*/)
{
    transferCityToStack(thisptr->dragDropInterf.phaseGame, &thisptr->data->fortificationId);
}

void __fastcall cityInterfTransferPotionsToStack(game::CCityStackInterf* thisptr, int /*%edx*/)
{
    transferCityToStack(thisptr->dragDropInterf.phaseGame, &thisptr->data->fortificationId,
                        isPotion);
}

void __fastcall cityInterfTransferSpellsToStack(game::CCityStackInterf* thisptr, int /*%edx*/)
{
    transferCityToStack(thisptr->dragDropInterf.phaseGame, &thisptr->data->fortificationId,
                        isSpell);
}

static bool isItemEquipped(const game::IdVector& equippedItems, const game::CMidgardID* itemId)
{
    for (const game::CMidgardID* item = equippedItems.bgn; item != equippedItems.end; item++) {
        if (*item == *itemId) {
            return true;
        }
    }

    return false;
}

/** Transfers visiting stack items to city. */
static void transferStackToCity(game::CPhaseGame* phaseGame,
                                const game::CMidgardID* cityId,
                                std::optional<ItemFilter> itemFilter = std::nullopt)
{
    using namespace game;

    auto objectMap = CPhaseApi::get().getObjectMap(&phaseGame->phase);
    auto obj = objectMap->vftable->findScenarioObjectById(objectMap, cityId);
    if (!obj) {
        logError("mssProxyError.log", fmt::format("Could not find city {:s}", idToString(cityId)));
        return;
    }

    auto fortification = static_cast<CFortification*>(obj);
    if (fortification->stackId == emptyId) {
        return;
    }

    auto stackObj = objectMap->vftable->findScenarioObjectById(objectMap, &fortification->stackId);
    if (!stackObj) {
        logError("mssProxyError.log",
                 fmt::format("Could not find stack {:s}", idToString(&fortification->stackId)));
        return;
    }

    const auto dynamicCast = RttiApi::get().dynamicCast;
    const auto& rtti = RttiApi::rtti();

    auto stack = (CMidStack*)dynamicCast(stackObj, 0, rtti.IMidScenarioObjectType,
                                         rtti.CMidStackType, 0);
    if (!stack) {
        logError("mssProxyError.log", fmt::format("Failed to cast scenario oject {:s} to stack",
                                                  idToString(&fortification->stackId)));
        return;
    }

    auto& inventory = stack->inventory;
    std::vector<CMidgardID> items;
    const int itemsTotal = inventory.vftable->getItemsCount(&inventory);
    for (int i = 0; i < itemsTotal; i++) {
        auto item = inventory.vftable->getItem(&inventory, i);
        if (isItemEquipped(stack->leaderEquppedItems, item)) {
            continue;
        }

        if (!itemFilter || (itemFilter && (*itemFilter)(objectMap, item))) {
            items.push_back(*item);
        }
    }

    transferItems(items, phaseGame, cityId, "city", &fortification->stackId, "stack");
}

void __fastcall cityInterfTransferAllToCity(game::CCityStackInterf* thisptr, int /*%edx*/)
{
    transferStackToCity(thisptr->dragDropInterf.phaseGame, &thisptr->data->fortificationId);
}

void __fastcall cityInterfTransferPotionsToCity(game::CCityStackInterf* thisptr, int /*%edx*/)
{
    transferStackToCity(thisptr->dragDropInterf.phaseGame, &thisptr->data->fortificationId,
                        isPotion);
}

void __fastcall cityInterfTransferSpellsToCity(game::CCityStackInterf* thisptr, int /*%edx*/)
{
    transferStackToCity(thisptr->dragDropInterf.phaseGame, &thisptr->data->fortificationId,
                        isSpell);
}

game::CCityStackInterf* __fastcall cityStackInterfCtorHooked(game::CCityStackInterf* thisptr,
                                                             int /*%edx*/,
                                                             void* taskOpenInterf,
                                                             game::CPhaseGame* phaseGame,
                                                             game::CMidgardID* cityId)
{
    using namespace game;

    const auto& cityStackInterf = CCityStackInterfApi::get();
    cityStackInterf.constructor(thisptr, taskOpenInterf, phaseGame, cityId);

    const auto& button = CButtonInterfApi::get();
    const auto freeFunctor = FunctorApi::get().createOrFree;
    const char dialogName[] = "DLG_CITY_STACK";

    using ButtonCallback = CCityStackInterfApi::Api::ButtonCallback;

    ButtonCallback callback{};
    callback.callback = (ButtonCallback::Callback)cityInterfTransferAllToStack;

    Functor functor;
    cityStackInterf.createButtonFunctor(&functor, 0, thisptr, &callback);

    auto dialog = CMidDragDropInterfApi::get().getDialog(&thisptr->dragDropInterf);
    button.assignFunctor(dialog, "BTN_TRANSF_L_ALL", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    callback.callback = (ButtonCallback::Callback)cityInterfTransferAllToCity;
    cityStackInterf.createButtonFunctor(&functor, 0, thisptr, &callback);
    button.assignFunctor(dialog, "BTN_TRANSF_R_ALL", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    callback.callback = (ButtonCallback::Callback)cityInterfTransferPotionsToStack;
    cityStackInterf.createButtonFunctor(&functor, 0, thisptr, &callback);
    button.assignFunctor(dialog, "BTN_TRANSF_L_POTIONS", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    callback.callback = (ButtonCallback::Callback)cityInterfTransferPotionsToCity;
    cityStackInterf.createButtonFunctor(&functor, 0, thisptr, &callback);
    button.assignFunctor(dialog, "BTN_TRANSF_R_POTIONS", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    callback.callback = (ButtonCallback::Callback)cityInterfTransferSpellsToStack;
    cityStackInterf.createButtonFunctor(&functor, 0, thisptr, &callback);
    button.assignFunctor(dialog, "BTN_TRANSF_L_SPELLS", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    callback.callback = (ButtonCallback::Callback)cityInterfTransferSpellsToCity;
    cityStackInterf.createButtonFunctor(&functor, 0, thisptr, &callback);
    button.assignFunctor(dialog, "BTN_TRANSF_R_SPELLS", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    return thisptr;
}

/** Transfers items from stack with srcStackId to stack with dstStackId. */
static void transferStackToStack(game::CPhaseGame* phaseGame,
                                 const game::CMidgardID* dstStackId,
                                 const game::CMidgardID* srcStackId,
                                 std::optional<ItemFilter> itemFilter = std::nullopt)
{
    using namespace game;

    auto objectMap = CPhaseApi::get().getObjectMap(&phaseGame->phase);
    auto srcObj = objectMap->vftable->findScenarioObjectById(objectMap, srcStackId);
    if (!srcObj) {
        logError("mssProxyError.log",
                 fmt::format("Could not find stack {:s}", idToString(srcStackId)));
        return;
    }

    const auto dynamicCast = RttiApi::get().dynamicCast;
    const auto& rtti = RttiApi::rtti();

    auto srcStack = (CMidStack*)dynamicCast(srcObj, 0, rtti.IMidScenarioObjectType,
                                            rtti.CMidStackType, 0);
    if (!srcStack) {
        logError("mssProxyError.log", fmt::format("Failed to cast scenario oject {:s} to stack",
                                                  idToString(srcStackId)));
        return;
    }

    auto& inventory = srcStack->inventory;
    std::vector<CMidgardID> items;
    const int itemsTotal = inventory.vftable->getItemsCount(&inventory);
    for (int i = 0; i < itemsTotal; i++) {
        auto item = inventory.vftable->getItem(&inventory, i);
        if (isItemEquipped(srcStack->leaderEquppedItems, item)) {
            continue;
        }

        if (!itemFilter || (itemFilter && (*itemFilter)(objectMap, item))) {
            items.push_back(*item);
        }
    }

    transferItems(items, phaseGame, dstStackId, "stack", srcStackId, "stack");
}

void __fastcall exchangeTransferAllToLeftStack(game::CExchangeInterf* thisptr, int /*%edx*/)
{
    transferStackToStack(thisptr->dragDropInterf.phaseGame, &thisptr->data->stackLeftSideId,
                         &thisptr->data->stackRightSideId);
}

void __fastcall exchangeTransferPotionsToLeftStack(game::CExchangeInterf* thisptr, int /*%edx*/)
{
    transferStackToStack(thisptr->dragDropInterf.phaseGame, &thisptr->data->stackLeftSideId,
                         &thisptr->data->stackRightSideId, isPotion);
}

void __fastcall exchangeTransferSpellsToLeftStack(game::CExchangeInterf* thisptr, int /*%edx*/)
{
    transferStackToStack(thisptr->dragDropInterf.phaseGame, &thisptr->data->stackLeftSideId,
                         &thisptr->data->stackRightSideId, isSpell);
}

void __fastcall exchangeTransferAllToRightStack(game::CExchangeInterf* thisptr, int /*%edx*/)
{
    transferStackToStack(thisptr->dragDropInterf.phaseGame, &thisptr->data->stackRightSideId,
                         &thisptr->data->stackLeftSideId);
}

void __fastcall exchangeTransferPotionsToRightStack(game::CExchangeInterf* thisptr, int /*%edx*/)
{
    transferStackToStack(thisptr->dragDropInterf.phaseGame, &thisptr->data->stackRightSideId,
                         &thisptr->data->stackLeftSideId, isPotion);
}

void __fastcall exchangeTransferSpellsToRightStack(game::CExchangeInterf* thisptr, int /*%edx*/)
{
    transferStackToStack(thisptr->dragDropInterf.phaseGame, &thisptr->data->stackRightSideId,
                         &thisptr->data->stackLeftSideId, isSpell);
}

game::CExchangeInterf* __fastcall exchangeInterfCtorHooked(game::CExchangeInterf* thisptr,
                                                           int /*%edx*/,
                                                           void* taskOpenInterf,
                                                           game::CPhaseGame* phaseGame,
                                                           game::CMidgardID* stackLeftSide,
                                                           game::CMidgardID* stackRightSide)
{
    using namespace game;

    const auto& exchangeInterf = CExchangeInterfApi::get();
    exchangeInterf.constructor(thisptr, taskOpenInterf, phaseGame, stackLeftSide, stackRightSide);

    const auto& button = CButtonInterfApi::get();
    const auto freeFunctor = FunctorApi::get().createOrFree;
    const char dialogName[] = "DLG_EXCHANGE";

    using ButtonCallback = CExchangeInterfApi::Api::ButtonCallback;

    ButtonCallback callback{};
    callback.callback = (ButtonCallback::Callback)exchangeTransferAllToLeftStack;

    Functor functor;
    exchangeInterf.createButtonFunctor(&functor, 0, thisptr, &callback);

    auto dialog = CMidDragDropInterfApi::get().getDialog(&thisptr->dragDropInterf);
    button.assignFunctor(dialog, "BTN_TRANSF_L_ALL", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    callback.callback = (ButtonCallback::Callback)exchangeTransferAllToRightStack;
    exchangeInterf.createButtonFunctor(&functor, 0, thisptr, &callback);
    button.assignFunctor(dialog, "BTN_TRANSF_R_ALL", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    callback.callback = (ButtonCallback::Callback)exchangeTransferPotionsToLeftStack;
    exchangeInterf.createButtonFunctor(&functor, 0, thisptr, &callback);
    button.assignFunctor(dialog, "BTN_TRANSF_L_POTIONS", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    callback.callback = (ButtonCallback::Callback)exchangeTransferPotionsToRightStack;
    exchangeInterf.createButtonFunctor(&functor, 0, thisptr, &callback);
    button.assignFunctor(dialog, "BTN_TRANSF_R_POTIONS", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    callback.callback = (ButtonCallback::Callback)exchangeTransferSpellsToLeftStack;
    exchangeInterf.createButtonFunctor(&functor, 0, thisptr, &callback);
    button.assignFunctor(dialog, "BTN_TRANSF_L_SPELLS", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    callback.callback = (ButtonCallback::Callback)exchangeTransferSpellsToRightStack;
    exchangeInterf.createButtonFunctor(&functor, 0, thisptr, &callback);
    button.assignFunctor(dialog, "BTN_TRANSF_R_SPELLS", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    return thisptr;
}

/** Transfers bag items to stack. */
static void transferBagToStack(game::CPhaseGame* phaseGame,
                               const game::CMidgardID* stackId,
                               const game::CMidgardID* bagId,
                               std::optional<ItemFilter> itemFilter = std::nullopt)
{
    using namespace game;

    auto objectMap = CPhaseApi::get().getObjectMap(&phaseGame->phase);
    auto bagObj = objectMap->vftable->findScenarioObjectById(objectMap, bagId);
    if (!bagObj) {
        logError("mssProxyError.log", fmt::format("Could not find bag {:s}", idToString(bagId)));
        return;
    }

    auto bag = static_cast<CMidBag*>(bagObj);
    auto& inventory = bag->inventory;
    std::vector<CMidgardID> items;
    const int itemsTotal = inventory.vftable->getItemsCount(&inventory);
    for (int i = 0; i < itemsTotal; i++) {
        auto item = inventory.vftable->getItem(&inventory, i);
        if (!itemFilter || (itemFilter && (*itemFilter)(objectMap, item))) {
            items.push_back(*item);
        }
    }

    transferItems(items, phaseGame, stackId, "stack", bagId, "bag");
}

void __fastcall pickupTransferAllToStack(game::CPickUpDropInterf* thisptr, int /*%edx*/)
{
    transferBagToStack(thisptr->dragDropInterf.phaseGame, &thisptr->data->stackId,
                       &thisptr->data->bagId);
}

void __fastcall pickupTransferPotionsToStack(game::CPickUpDropInterf* thisptr, int /*%edx*/)
{
    transferBagToStack(thisptr->dragDropInterf.phaseGame, &thisptr->data->stackId,
                       &thisptr->data->bagId, isPotion);
}

void __fastcall pickupTransferSpellsToStack(game::CPickUpDropInterf* thisptr, int /*%edx*/)
{
    transferBagToStack(thisptr->dragDropInterf.phaseGame, &thisptr->data->stackId,
                       &thisptr->data->bagId, isSpell);
}

/** Transfers stack items to bag. */
static void transferStackToBag(game::CPhaseGame* phaseGame,
                               const game::CMidgardID* stackId,
                               const game::CMidgardID* bagId,
                               std::optional<ItemFilter> itemFilter = std::nullopt)
{
    using namespace game;

    auto objectMap = CPhaseApi::get().getObjectMap(&phaseGame->phase);
    auto stackObj = objectMap->vftable->findScenarioObjectById(objectMap, stackId);
    if (!stackObj) {
        logError("mssProxyError.log",
                 fmt::format("Could not find stack {:s}", idToString(stackId)));
        return;
    }

    const auto dynamicCast = RttiApi::get().dynamicCast;
    const auto& rtti = RttiApi::rtti();

    auto stack = (CMidStack*)dynamicCast(stackObj, 0, rtti.IMidScenarioObjectType,
                                         rtti.CMidStackType, 0);
    if (!stack) {
        logError("mssProxyError.log",
                 fmt::format("Failed to cast scenario oject {:s} to stack", idToString(stackId)));
        return;
    }

    auto& inventory = stack->inventory;
    std::vector<CMidgardID> items;
    const int itemsTotal = inventory.vftable->getItemsCount(&inventory);
    for (int i = 0; i < itemsTotal; i++) {
        auto item = inventory.vftable->getItem(&inventory, i);
        if (isItemEquipped(stack->leaderEquppedItems, item)) {
            continue;
        }

        if (!itemFilter || (itemFilter && (*itemFilter)(objectMap, item))) {
            items.push_back(*item);
        }
    }

    transferItems(items, phaseGame, bagId, "bag", stackId, "stack");
}

void __fastcall pickupTransferAllToBag(game::CPickUpDropInterf* thisptr, int /*%edx*/)
{
    transferStackToBag(thisptr->dragDropInterf.phaseGame, &thisptr->data->stackId,
                       &thisptr->data->bagId);
}

void __fastcall pickupTransferPotionsToBag(game::CPickUpDropInterf* thisptr, int /*%edx*/)
{
    transferStackToBag(thisptr->dragDropInterf.phaseGame, &thisptr->data->stackId,
                       &thisptr->data->bagId, isPotion);
}

void __fastcall pickupTransferSpellsToBag(game::CPickUpDropInterf* thisptr, int /*%edx*/)
{
    transferStackToBag(thisptr->dragDropInterf.phaseGame, &thisptr->data->stackId,
                       &thisptr->data->bagId, isSpell);
}

game::CPickUpDropInterf* __fastcall pickupDropInterfCtorHooked(game::CPickUpDropInterf* thisptr,
                                                               int /*%edx*/,
                                                               void* taskOpenInterf,
                                                               game::CPhaseGame* phaseGame,
                                                               game::CMidgardID* stackId,
                                                               game::CMidgardID* bagId)
{
    using namespace game;

    const auto& pickupInterf = CPickUpDropInterfApi::get();
    pickupInterf.constructor(thisptr, taskOpenInterf, phaseGame, stackId, bagId);

    const auto& button = CButtonInterfApi::get();
    const auto freeFunctor = FunctorApi::get().createOrFree;
    const char dialogName[] = "DLG_PICKUP_DROP";

    using ButtonCallback = CPickUpDropInterfApi::Api::ButtonCallback;

    ButtonCallback callback{};
    callback.callback = (ButtonCallback::Callback)pickupTransferAllToStack;

    Functor functor;
    pickupInterf.createButtonFunctor(&functor, 0, thisptr, &callback);

    auto dialog = CMidDragDropInterfApi::get().getDialog(&thisptr->dragDropInterf);
    button.assignFunctor(dialog, "BTN_TRANSF_L_ALL", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    callback.callback = (ButtonCallback::Callback)pickupTransferAllToBag;
    pickupInterf.createButtonFunctor(&functor, 0, thisptr, &callback);
    button.assignFunctor(dialog, "BTN_TRANSF_R_ALL", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    callback.callback = (ButtonCallback::Callback)pickupTransferPotionsToStack;
    pickupInterf.createButtonFunctor(&functor, 0, thisptr, &callback);
    button.assignFunctor(dialog, "BTN_TRANSF_L_POTIONS", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    callback.callback = (ButtonCallback::Callback)pickupTransferPotionsToBag;
    pickupInterf.createButtonFunctor(&functor, 0, thisptr, &callback);
    button.assignFunctor(dialog, "BTN_TRANSF_R_POTIONS", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    callback.callback = (ButtonCallback::Callback)pickupTransferSpellsToStack;
    pickupInterf.createButtonFunctor(&functor, 0, thisptr, &callback);
    button.assignFunctor(dialog, "BTN_TRANSF_L_SPELLS", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    callback.callback = (ButtonCallback::Callback)pickupTransferSpellsToBag;
    pickupInterf.createButtonFunctor(&functor, 0, thisptr, &callback);
    button.assignFunctor(dialog, "BTN_TRANSF_R_SPELLS", dialogName, &functor, 0);
    freeFunctor(&functor, nullptr);

    return thisptr;
}

} // namespace hooks
