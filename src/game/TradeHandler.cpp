/*
 * Copyright (C) 2005-2008 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2008 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2008-2017 Hellground <http://wow-hellground.com/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "Common.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "World.h"
#include "ObjectAccessor.h"
#include "Log.h"
#include "Opcodes.h"
#include "Player.h"
#include "Item.h"
#include "SocialMgr.h"
#include "Language.h"

#include "AccountMgr.h"

void WorldSession::SendTradeStatus(uint32 status)
{
    WorldPacket data;

    switch (status)
    {
        case TRADE_STATUS_BEGIN_TRADE:
            data.Initialize(SMSG_TRADE_STATUS, 4+8);
            data << uint32(status);
            data << uint64(0);
            break;
        case TRADE_STATUS_OPEN_WINDOW:
            data.Initialize(SMSG_TRADE_STATUS, 4+4);
            data << uint32(status);
            data << uint32(0);                              // added in 2.4.0
            break;
        case TRADE_STATUS_CLOSE_WINDOW:
            data.Initialize(SMSG_TRADE_STATUS, 4+4+1+4);
            data << uint32(status);
            data << uint32(0);
            data << uint8(0);
            data << uint32(0);
            break;
        case TRADE_STATUS_ONLY_CONJURED:
            data.Initialize(SMSG_TRADE_STATUS, 4+1);
            data << uint32(status);
            data << uint8(0);
            break;
        default:
            data.Initialize(SMSG_TRADE_STATUS, 4);
            data << uint32(status);
            break;
    }

    SendPacket(&data);
}

void WorldSession::HandleIgnoreTradeOpcode(WorldPacket& /*recvPacket*/)
{
    sLog.outDebug("WORLD: Ignore Trade %u",_player->GetGUIDLow());
    // recvPacket.print_storage();
}

void WorldSession::HandleBusyTradeOpcode(WorldPacket& /*recvPacket*/)
{
    sLog.outDebug("WORLD: Busy Trade %u",_player->GetGUIDLow());
    // recvPacket.print_storage();
}

void WorldSession::SendUpdateTrade()
{
    Item *item = NULL;

    if (!_player || !_player->pTrader)
        return;

    // reset trade status
    if (_player->acceptTrade)
    {
        _player->acceptTrade = false;
        SendTradeStatus(TRADE_STATUS_BACK_TO_TRADE);
    }

    if (_player->pTrader->acceptTrade)
    {
        _player->pTrader->acceptTrade = false;
        _player->pTrader->GetSession()->SendTradeStatus(TRADE_STATUS_BACK_TO_TRADE);
    }

    WorldPacket data(SMSG_TRADE_STATUS_EXTENDED, (100));    // guess size
    data << (uint8) 1;                                     // can be different (only seen 0 and 1)
    data << (uint32) 0;                                     // added in 2.4.0, this value must be equal to value from TRADE_STATUS_OPEN_WINDOW status packet (different value for different players to block multiple trades?)
    data << (uint32) TRADE_SLOT_COUNT;                      // trade slots count/number?, = next field in most cases
    data << (uint32) TRADE_SLOT_COUNT;                      // trade slots count/number?, = prev field in most cases
    data << (uint32) _player->pTrader->tradeGold;           // trader gold
    data << (uint32) 0;                                     // spell cast on lowest slot item

    for (uint8 i = 0; i < TRADE_SLOT_COUNT; i++)
    {
        item = (_player->pTrader->tradeItems[i] != NULL_SLOT ? _player->pTrader->GetItemByPos(_player->pTrader->tradeItems[i]) : NULL);

        data << (uint8) i;                                  // trade slot number, if not specified, then end of packet

        if (item)
        {
            data << (uint32) item->GetProto()->ItemId;      // entry
                                                            // display id
            data << (uint32) item->GetProto()->DisplayInfoID;
                                                            // stack count
            data << (uint32) item->GetUInt32Value(ITEM_FIELD_STACK_COUNT);
            data << (uint32) 0;                             // probably gift=1, created_by=0?
                                                            // gift creator
            data << (uint64) item->GetUInt64Value(ITEM_FIELD_GIFTCREATOR);
            data << (uint32) item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT);
            for (uint8 j = 0; j < 3; ++j)
                data << (uint32) 0;                         // enchantment id (permanent/gems?)
                                                            // creator
            data << (uint64) item->GetUInt64Value(ITEM_FIELD_CREATOR);
            data << (uint32) item->GetSpellCharges();       // charges
            data << (uint32) item->GetItemSuffixFactor();   // SuffixFactor
                                                            // random properties id
            data << (uint32) item->GetItemRandomPropertyId();
            data << (uint32) item->GetProto()->LockID;      // lock id
                                                            // max durability
            data << (uint32) item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
                                                            // durability
            data << (uint32) item->GetUInt32Value(ITEM_FIELD_DURABILITY);
        }
        else
        {
            for (uint8 j = 0; j < 18; j++)
                data << uint32(0);
        }
    }
    SendPacket(&data);
}

//==============================================================
// transfer the items to the players

void WorldSession::moveItems(Item* myItems[], Item* hisItems[])
{
    for (int i=0; i<TRADE_SLOT_TRADED_COUNT; i++)
    {
        ItemPosCountVec traderDst;
        ItemPosCountVec playerDst;
        bool traderCanTrade = (myItems[i]==NULL || _player->pTrader->CanStoreItem(NULL_BAG, NULL_SLOT, traderDst, myItems[i], false) == EQUIP_ERR_OK);
        bool playerCanTrade = (hisItems[i]==NULL || _player->CanStoreItem(NULL_BAG, NULL_SLOT, playerDst, hisItems[i], false) == EQUIP_ERR_OK);
        if (traderCanTrade && playerCanTrade)
        {
            // Ok, if trade item exists and can be stored
            // If we trade in both directions we had to check, if the trade will work before we actually do it
            // A roll back is not possible after we stored it
            if (myItems[i])
            {
                // logging
                sLog.outDebug("partner storing: %u",myItems[i]->GetGUIDLow());
                if (_player->GetSession()->HasPermissions(PERM_GMT) && sWorld.getConfig(CONFIG_GM_LOG_TRADE))
                {
                    sLog.outCommand(_player->GetSession()->GetAccountId(),"GM %s (Account: %u) trade: %s (Entry: %d Count: %u) to player: %s (Account: %u)",
                        _player->GetName(),_player->GetSession()->GetAccountId(),
                        myItems[i]->GetProto()->Name1,myItems[i]->GetEntry(),myItems[i]->GetCount(),
                        _player->pTrader->GetName(),_player->pTrader->GetSession()->GetAccountId());
                }

                sLog.outLog(LOG_TRADE, "Player %s (Account: %u) trade: %s (Entry: %d Count: %u) to player: %s (Account: %u)",
                    _player->GetName(),_player->GetSession()->GetAccountId(),
                    myItems[i]->GetProto()->Name1,myItems[i]->GetEntry(),myItems[i]->GetCount(),
                    _player->pTrader->GetName(),_player->pTrader->GetSession()->GetAccountId());

                // store
                _player->pTrader->MoveItemToInventory(traderDst, myItems[i], true, true);
            }
            if (hisItems[i])
            {
                // logging
                sLog.outDebug("player storing: %u",hisItems[i]->GetGUIDLow());
                if (_player->pTrader->GetSession()->HasPermissions(PERM_GMT) && sWorld.getConfig(CONFIG_GM_LOG_TRADE))
                {
                    sLog.outCommand(_player->pTrader->GetSession()->GetAccountId(),"GM %s (Account: %u) trade: %s (Entry: %d Count: %u) to player: %s (Account: %u)",
                        _player->pTrader->GetName(),_player->pTrader->GetSession()->GetAccountId(),
                        hisItems[i]->GetProto()->Name1,hisItems[i]->GetEntry(),hisItems[i]->GetCount(),
                        _player->GetName(),_player->GetSession()->GetAccountId());
                }

                sLog.outLog(LOG_TRADE, "Player %s (Account: %u) trade: %s (Entry: %d Count: %u) to player: %s (Account: %u)",
                    _player->pTrader->GetName(),_player->pTrader->GetSession()->GetAccountId(),
                    hisItems[i]->GetProto()->Name1,hisItems[i]->GetEntry(),hisItems[i]->GetCount(),
                    _player->GetName(),_player->GetSession()->GetAccountId());

                // store
                _player->MoveItemToInventory(playerDst, hisItems[i], true, true);
            }
        }
        else
        {
            // in case of fatal error log error message
            // return the already removed items to the original owner
            if (myItems[i])
            {
                if (!traderCanTrade)
                    sLog.outLog(LOG_DEFAULT, "ERROR: trader can't store item: %u",myItems[i]->GetGUIDLow());
                if (_player->CanStoreItem(NULL_BAG, NULL_SLOT, playerDst, myItems[i], false) == EQUIP_ERR_OK)
                    _player->MoveItemToInventory(playerDst, myItems[i], true, true);
                else
                    sLog.outLog(LOG_DEFAULT, "ERROR: player can't take item back: %u",myItems[i]->GetGUIDLow());
            }
            // return the already removed items to the original owner
            if (hisItems[i])
            {
                if (!playerCanTrade)
                    sLog.outLog(LOG_DEFAULT, "ERROR: player can't store item: %u",hisItems[i]->GetGUIDLow());
                if (_player->pTrader->CanStoreItem(NULL_BAG, NULL_SLOT, traderDst, hisItems[i], false) == EQUIP_ERR_OK)
                    _player->pTrader->MoveItemToInventory(traderDst, hisItems[i], true, true);
                else
                    sLog.outLog(LOG_DEFAULT, "ERROR: trader can't take item back: %u",hisItems[i]->GetGUIDLow());
            }
        }
    }
}

//==============================================================

void WorldSession::HandleAcceptTradeOpcode(WorldPacket& /*recvPacket*/)
{
    Item *myItems[TRADE_SLOT_TRADED_COUNT]  = { NULL, NULL, NULL, NULL, NULL, NULL };
    Item *hisItems[TRADE_SLOT_TRADED_COUNT] = { NULL, NULL, NULL, NULL, NULL, NULL };
    bool myCanCompleteTrade=true,hisCanCompleteTrade=true;

    if (!GetPlayer()->pTrader)
        return;

    // not accept case incorrect money amount
    if (_player->tradeGold > _player->GetMoney())
    {
        SendNotification(LANG_NOT_ENOUGH_GOLD);
        _player->pTrader->GetSession()->SendTradeStatus(TRADE_STATUS_BACK_TO_TRADE);
        _player->acceptTrade = false;
        return;
    }

    // not accept case incorrect money amount
    if (_player->pTrader->tradeGold > _player->pTrader->GetMoney())
    {
        _player->pTrader->GetSession()->SendNotification(LANG_NOT_ENOUGH_GOLD);
        SendTradeStatus(TRADE_STATUS_BACK_TO_TRADE);
        _player->pTrader->acceptTrade = false;
        return;
    }

    // not accept if some items now can't be trade (cheating)
    for (int i=0; i<TRADE_SLOT_TRADED_COUNT; i++)
    {
        if (_player->tradeItems[i] != NULL_SLOT)
        {
            if (Item* item  =_player->GetItemByPos(_player->tradeItems[i]))
            {
                if (!item->CanBeTraded())
                {
                    SendTradeStatus(TRADE_STATUS_TRADE_CANCELED);
                    return;
                }
            }
            else
            {
                // if not found item and slot was other than NULL (cheating)
                // jeszcze jest jeden exploit na trade item�w innych ni� s� pokazane,
                // ale te cipy na to nie wpadn� jak im kto� nie da gotowego hacka
                SendTradeStatus(TRADE_STATUS_TRADE_CANCELED);

                std::string accountname;
                if (AccountMgr::GetName(_player->GetSession()->GetAccountId(), accountname))
                {
                    std::string duration = "-1";
                    std::string reason = "GM INFO - trade hack/exploit";
                    std::string name = "CONSOLE";

                    sWorld.BanAccount(BAN_ACCOUNT, accountname.c_str(), duration.c_str(), reason.c_str(), name.c_str());
                }
                sLog.outLog(LOG_EXPLOITS_CHEATS, "Player %s banned for trade exploit, no item found (trading with %s)", _player->GetName(), _player->pTrader->GetName());
                return;
            }
        }

        if (_player->pTrader->tradeItems[i] != NULL_SLOT)
        {
            if (Item* item  =_player->pTrader->GetItemByPos(_player->pTrader->tradeItems[i]))
            {
                if (!item->CanBeTraded())
                {
                    SendTradeStatus(TRADE_STATUS_TRADE_CANCELED);
                    return;
                }
            }
            else
            {
                SendTradeStatus(TRADE_STATUS_TRADE_CANCELED);

                std::string accountname;
                if (AccountMgr::GetName(_player->pTrader->GetSession()->GetAccountId(), accountname))
                {
                    std::string duration = "-1";
                    std::string reason = "GM INFO - trade hack/exploit";
                    std::string name = "CONSOLE";

                    sWorld.BanAccount(BAN_ACCOUNT, accountname.c_str(), duration.c_str(), reason.c_str(), name.c_str());
                }
                sLog.outLog(LOG_EXPLOITS_CHEATS, "Player %s banned for trade exploit, no item found (trading with %s)", _player->pTrader->GetName(), _player->GetName());
                return;
            }
        }
    }

    _player->acceptTrade = true;
    if (_player->pTrader->acceptTrade)
    {
        // inform partner client
        _player->pTrader->GetSession()->SendTradeStatus(TRADE_STATUS_TRADE_ACCEPT);

        // store items in local list and set 'in-trade' flag
        for (int i=0; i<TRADE_SLOT_TRADED_COUNT; i++)
        {
            if (_player->tradeItems[i] != NULL_SLOT)
            {
                sLog.outDebug("player trade item bag: %u slot: %u",_player->tradeItems[i] >> 8, _player->tradeItems[i] & 255);
                                                            //Can return NULL
                myItems[i]=_player->GetItemByPos(_player->tradeItems[i]);
                if (myItems[i])
                    myItems[i]->SetInTrade();
            }
            if (_player->pTrader->tradeItems[i] != NULL_SLOT)
            {
                sLog.outDebug("partner trade item bag: %u slot: %u",_player->pTrader->tradeItems[i] >> 8,_player->pTrader->tradeItems[i] & 255);
                                                            //Can return NULL
                hisItems[i]=_player->pTrader->GetItemByPos(_player->pTrader->tradeItems[i]);
                if (hisItems[i])
                    hisItems[i]->SetInTrade();
            }
        }

        // test if item will fit in each inventory
        hisCanCompleteTrade =  (_player->pTrader->CanStoreItems(myItems,TRADE_SLOT_TRADED_COUNT)== EQUIP_ERR_OK);
        myCanCompleteTrade = (_player->CanStoreItems(hisItems,TRADE_SLOT_TRADED_COUNT) == EQUIP_ERR_OK);

        // clear 'in-trade' flag
        for (int i=0; i<TRADE_SLOT_TRADED_COUNT; i++)
        {
            if (myItems[i])  myItems[i]->SetInTrade(false);
            if (hisItems[i]) hisItems[i]->SetInTrade(false);
        }

        // in case of missing space report error
        if (!myCanCompleteTrade)
        {
            SendNotification(LANG_NOT_FREE_TRADE_SLOTS);
            GetPlayer()->pTrader->GetSession()->SendNotification(LANG_NOT_PARTNER_FREE_TRADE_SLOTS);
            SendTradeStatus(TRADE_STATUS_BACK_TO_TRADE);
            _player->pTrader->GetSession()->SendTradeStatus(TRADE_STATUS_BACK_TO_TRADE);
            return;
        }
        else if (!hisCanCompleteTrade)
        {
            SendNotification(LANG_NOT_PARTNER_FREE_TRADE_SLOTS);
            GetPlayer()->pTrader->GetSession()->SendNotification(LANG_NOT_FREE_TRADE_SLOTS);
            SendTradeStatus(TRADE_STATUS_BACK_TO_TRADE);
            _player->pTrader->GetSession()->SendTradeStatus(TRADE_STATUS_BACK_TO_TRADE);
            return;
        }

        if (_player->tradeGold > 0)
        {
            if (_player->tradeGold > MAX_MONEY_AMOUNT/*just in case cheating?..*/ || _player->tradeGold + _player->pTrader->GetMoney() >= MAX_MONEY_AMOUNT)
            {
                _player->SendEquipError(EQUIP_ERR_TOO_MUCH_GOLD,NULL,NULL);
                _player->pTrader->SendEquipError(EQUIP_ERR_TOO_MUCH_GOLD,NULL,NULL);
                SendTradeStatus(TRADE_STATUS_BACK_TO_TRADE);
                _player->pTrader->GetSession()->SendTradeStatus(TRADE_STATUS_BACK_TO_TRADE);
                return;
            }
            sLog.outLog(LOG_TRADE, "Player %s (Account: %u) give money (Amount: %u) to player: %s (Account: %u)",
                _player->GetName(),_player->GetSession()->GetAccountId(),
                _player->tradeGold,
                _player->pTrader->GetName(),_player->pTrader->GetSession()->GetAccountId());
        }
        if (_player->pTrader->tradeGold > 0)
        {
            if (_player->pTrader->tradeGold > MAX_MONEY_AMOUNT/*just in case cheating?..*/ || _player->pTrader->tradeGold + _player->GetMoney() >= MAX_MONEY_AMOUNT)
            {
                _player->SendEquipError(EQUIP_ERR_TOO_MUCH_GOLD,NULL,NULL);
                _player->pTrader->SendEquipError(EQUIP_ERR_TOO_MUCH_GOLD,NULL,NULL);
                SendTradeStatus(TRADE_STATUS_BACK_TO_TRADE);
                _player->pTrader->GetSession()->SendTradeStatus(TRADE_STATUS_BACK_TO_TRADE);
                return;
            }
            sLog.outLog(LOG_TRADE, "Player %s (Account: %u) give money (Amount: %u) to player: %s (Account: %u)",
                _player->pTrader->GetName(),_player->pTrader->GetSession()->GetAccountId(),
                _player->pTrader->tradeGold,
                _player->GetName(),_player->GetSession()->GetAccountId());
        }

        // execute trade: 1. remove
        for (int i=0; i<TRADE_SLOT_TRADED_COUNT; i++)
        {
            if (myItems[i])
            {
                myItems[i]->SetUInt64Value(ITEM_FIELD_GIFTCREATOR,_player->GetGUID());
                _player->MoveItemFromInventory(_player->tradeItems[i] >> 8, _player->tradeItems[i] & 255, true);
            }
            if (hisItems[i])
            {
                hisItems[i]->SetUInt64Value(ITEM_FIELD_GIFTCREATOR,_player->pTrader->GetGUID());
                _player->pTrader->MoveItemFromInventory(_player->pTrader->tradeItems[i] >> 8, _player->pTrader->tradeItems[i] & 255, true);
            }
        }

        // execute trade: 2. store
        moveItems(myItems, hisItems);

        // logging money
        if (sWorld.getConfig(CONFIG_GM_LOG_TRADE))
        {
            if (_player->GetSession()->HasPermissions(PERM_GMT) && _player->tradeGold > 0)
            {
                sLog.outCommand(_player->GetSession()->GetAccountId(),"GM %s (Account: %u) give money (Amount: %u) to player: %s (Account: %u)",
                    _player->GetName(),_player->GetSession()->GetAccountId(),
                    _player->tradeGold,
                    _player->pTrader->GetName(),_player->pTrader->GetSession()->GetAccountId());
            }
            if (_player->pTrader->GetSession()->HasPermissions(PERM_GMT) && _player->pTrader->tradeGold > 0)
            {
                sLog.outCommand(_player->pTrader->GetSession()->GetAccountId(),"GM %s (Account: %u) give money (Amount: %u) to player: %s (Account: %u)",
                    _player->pTrader->GetName(),_player->pTrader->GetSession()->GetAccountId(),
                    _player->pTrader->tradeGold,
                    _player->GetName(),_player->GetSession()->GetAccountId());
            }
        }

        // update money
        _player->ModifyMoney(-int32(_player->tradeGold));
        _player->ModifyMoney(_player->pTrader->tradeGold);
        _player->pTrader->ModifyMoney(-int32(_player->pTrader->tradeGold));
        _player->pTrader->ModifyMoney(_player->tradeGold);

        _player->ClearTrade();
        _player->pTrader->ClearTrade();

        // desynchronized with the other saves here (SaveInventoryAndGoldToDB() not have own transaction guards)
        RealmDataDatabase.BeginTransaction();
        _player->SaveInventoryAndGoldToDB();
        _player->pTrader->SaveInventoryAndGoldToDB();
        RealmDataDatabase.CommitTransaction();

        _player->pTrader->GetSession()->SendTradeStatus(TRADE_STATUS_TRADE_COMPLETE);
        SendTradeStatus(TRADE_STATUS_TRADE_COMPLETE);

        _player->pTrader->pTrader = NULL;
        _player->pTrader = NULL;
    }
    else
    {
        _player->pTrader->GetSession()->SendTradeStatus(TRADE_STATUS_TRADE_ACCEPT);
    }
}

void WorldSession::HandleUnacceptTradeOpcode(WorldPacket& /*recvPacket*/)
{
    if (!GetPlayer()->pTrader)
        return;

    _player->pTrader->GetSession()->SendTradeStatus(TRADE_STATUS_BACK_TO_TRADE);
    _player->acceptTrade = false;
}

void WorldSession::HandleBeginTradeOpcode(WorldPacket& /*recvPacket*/)
{
    if (!_player->pTrader)
        return;

    _player->pTrader->GetSession()->SendTradeStatus(TRADE_STATUS_OPEN_WINDOW);
    _player->pTrader->ClearTrade();

    SendTradeStatus(TRADE_STATUS_OPEN_WINDOW);
    _player->ClearTrade();
}

void WorldSession::SendCancelTrade()
{
    SendTradeStatus(TRADE_STATUS_TRADE_CANCELED);
}

void WorldSession::HandleCancelTradeOpcode(WorldPacket& /*recvPacket*/)
{
    // sended also after LOGOUT COMPLETE
    if (_player)                                             // needed because STATUS_AUTHED
        _player->TradeCancel(true);
}

void WorldSession::HandleInitiateTradeOpcode(WorldPacket& recvPacket)
{
    CHECK_PACKET_SIZE(recvPacket,8);

    if (GetPlayer()->pTrader)
        return;

    uint64 ID;

    if (!GetPlayer()->IsAlive())
    {
        SendTradeStatus(TRADE_STATUS_YOU_DEAD);
        return;
    }

    if (GetPlayer()->HasUnitState(UNIT_STAT_STUNNED))
    {
        SendTradeStatus(TRADE_STATUS_YOU_STUNNED);
        return;
    }

    if (isLogingOut())
    {
        SendTradeStatus(TRADE_STATUS_YOU_LOGOUT);
        return;
    }

    if (GetPlayer()->IsTaxiFlying())
    {
        SendTradeStatus(TRADE_STATUS_TARGET_TO_FAR);
        return;
    }

    recvPacket >> ID;

    Player* pOther = ObjectAccessor::FindPlayer(ID);

    if (!pOther)
    {
        SendTradeStatus(TRADE_STATUS_NO_TARGET);
        return;
    }

    if (pOther == GetPlayer() || pOther->pTrader)
    {
        SendTradeStatus(TRADE_STATUS_BUSY);
        return;
    }

    if (!pOther->IsAlive())
    {
        SendTradeStatus(TRADE_STATUS_TARGET_DEAD);
        return;
    }

    if (pOther->IsTaxiFlying())
    {
        SendTradeStatus(TRADE_STATUS_TARGET_TO_FAR);
        return;
    }

    if (pOther->HasUnitState(UNIT_STAT_STUNNED))
    {
        SendTradeStatus(TRADE_STATUS_TARGET_STUNNED);
        return;
    }

    if (pOther->GetSession()->isLogingOut())
    {
        SendTradeStatus(TRADE_STATUS_TARGET_LOGOUT);
        return;
    }

    if (pOther->GetSocial()->HasIgnore(GetPlayer()->GetGUIDLow()))
    {
        SendTradeStatus(TRADE_STATUS_IGNORE_YOU);
        return;
    }

    if (!sWorld.getConfig(CONFIG_ALLOW_TWO_SIDE_TRADE) && pOther->GetTeam() !=_player->GetTeam())
    {
        SendTradeStatus(TRADE_STATUS_WRONG_FACTION);
        return;
    }

    if (pOther->GetDistance2d(_player) > 10.0f)
    {
        SendTradeStatus(TRADE_STATUS_TARGET_TO_FAR);
        return;
    }

    // OK start trade
    _player->pTrader = pOther;
    pOther->pTrader =_player;

    WorldPacket data(SMSG_TRADE_STATUS, 12);
    data << (uint32) TRADE_STATUS_BEGIN_TRADE;
    data << (uint64)_player->GetGUID();
    _player->pTrader->SendPacketToSelf(&data);
}

void WorldSession::HandleSetTradeGoldOpcode(WorldPacket& recvPacket)
{
    CHECK_PACKET_SIZE(recvPacket,4);

    if (!_player->pTrader)
        return;

    uint32 gold;

    recvPacket >> gold;

    // gold can be incorrect, but this is checked at trade finished.
    _player->tradeGold = gold;

    _player->pTrader->GetSession()->SendUpdateTrade();
}

void WorldSession::HandleSetTradeItemOpcode(WorldPacket& recvPacket)
{
    CHECK_PACKET_SIZE(recvPacket,1+1+1);

    if (!_player->pTrader)
        return;

    // send update
    uint8 tradeSlot;
    uint8 bag;
    uint8 slot;

    recvPacket >> tradeSlot;
    recvPacket >> bag;
    recvPacket >> slot;

    // invalid slot number
    if (tradeSlot >= TRADE_SLOT_COUNT)
    {
        SendTradeStatus(TRADE_STATUS_TRADE_CANCELED);
        return;
    }

    // check cheating, can't fail with correct client operations
    Item* item = _player->GetItemByPos(bag,slot);
    if (!item || tradeSlot!=TRADE_SLOT_NONTRADED && !item->CanBeTraded())
    {
        SendTradeStatus(TRADE_STATUS_TRADE_CANCELED);
        return;
    }

    uint16 pos = (bag << 8) | slot;

    // prevent place single item into many trade slots using cheating and client bugs
    for (int i = 0; i < TRADE_SLOT_COUNT; ++i)
    {
        if (_player->tradeItems[i]==pos)
        {
            // cheating attempt
            SendTradeStatus(TRADE_STATUS_TRADE_CANCELED);
            return;
        }
    }

    _player->tradeItems[tradeSlot] = pos;

    _player->pTrader->GetSession()->SendUpdateTrade();
}

void WorldSession::HandleClearTradeItemOpcode(WorldPacket& recvPacket)
{
    CHECK_PACKET_SIZE(recvPacket,1);

    if (!_player->pTrader)
        return;

    uint8 tradeSlot;
    recvPacket >> tradeSlot;

    // invalid slot number
    if (tradeSlot >= TRADE_SLOT_COUNT)
        return;

    _player->tradeItems[tradeSlot] = NULL_SLOT;

    _player->pTrader->GetSession()->SendUpdateTrade();
}

