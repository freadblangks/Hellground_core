/* 
 * Copyright (C) 2006-2008 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
 * Copyright (C) 2008-2015 Hellground <http://hellground.net/>
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

/* ScriptData
SDName: Instance_Shadow_Labyrinth
SD%Complete: 85
SDComment: Some cleanup left along with save
SDCategory: Auchindoun, Shadow Labyrinth
EndScriptData */

#include "precompiled.h"
#include "def_shadow_labyrinth.h"

#define ENCOUNTERS 4

#define REFECTORY_DOOR          183296                      //door opened when blackheart the inciter dies
#define SCREAMING_HALL_DOOR     183295                      //door opened when grandmaster vorpil dies

/* Shadow Labyrinth encounters:
1 - Ambassador Hellmaw event
2 - Blackheart the Inciter event
3 - Grandmaster Vorpil event
4 - Murmur event
*/

struct instance_shadow_labyrinth : public ScriptedInstance
{
    instance_shadow_labyrinth(Map *map) : ScriptedInstance(map) {Initialize();};

    uint32 Encounter[ENCOUNTERS];

    uint64 RefectoryDoorGUID;
    uint64 ScreamingHallDoorGUID;

    uint64 GrandmasterVorpil;

    void Initialize()
    {
        RefectoryDoorGUID = 0;
        ScreamingHallDoorGUID = 0;

        GrandmasterVorpil = 0;

        for(uint8 i = 0; i < ENCOUNTERS; i++)
            Encounter[i] = NOT_STARTED;
    }

    bool IsEncounterInProgress() const
    {
        for(uint8 i = 0; i < ENCOUNTERS; i++)
            if(Encounter[i] == IN_PROGRESS) return true;

        return false;
    }

    void OnObjectCreate(GameObject *go)
    {
        switch(go->GetEntry())
        {
        case REFECTORY_DOOR:
            RefectoryDoorGUID = go->GetGUID();
            if (Encounter[1] == DONE)
                go->SetGoState(GO_STATE_ACTIVE);
            break;
        case SCREAMING_HALL_DOOR:
            ScreamingHallDoorGUID = go->GetGUID();
            if (Encounter[2] == DONE)
                go->SetGoState(GO_STATE_ACTIVE);
            break;
        }
    }

    void OnCreatureCreate(Creature *creature, uint32 creature_entry)
    {
        switch(creature_entry)
        {
            case 18732:
                GrandmasterVorpil = creature->GetGUID();
                break;
        }
    }

    void HandleGameObject(uint64 guid, uint32 state)
    {
        if (!guid)
        {
            debug_log("TSCR: Shadow Labyrinth: HandleGameObject fail");
            return;
        }

        if (GameObject *go = instance->GetGameObject(guid))
            go->SetGoState(GOState(state));
    }

    void SetData(uint32 type, uint32 data)
    {
        switch(type)
        {
            case TYPE_HELLMAW:
                Encounter[0] = data;
                break;
            case DATA_BLACKHEARTTHEINCITEREVENT:
                if (data == DONE)
                    HandleGameObject(RefectoryDoorGUID,0);
                Encounter[1] = data;
                break;

            case DATA_GRANDMASTERVORPILEVENT:
                if (data == DONE)
                    HandleGameObject(ScreamingHallDoorGUID,0);
                Encounter[2] = data;
                break;

            case DATA_MURMUREVENT:
                Encounter[3] = data;
                break;
            case TYPE_RITUALIST: // handled in getdata
                break;
        }

        if (data == DONE)
        {

            SaveToDB();
        }
    }

    uint32 GetData(uint32 type)
    {
        switch (type)
        {
            case TYPE_HELLMAW:                  return Encounter[0];
            case TYPE_RITUALIST:
            {
                if (instance->GetCreatureById(18794, GET_ALIVE_CREATURE_GUID))
                    return NOT_STARTED;
                else return DONE;
            }
            case DATA_GRANDMASTERVORPILEVENT:   return Encounter[2];
            case DATA_MURMUREVENT:              return Encounter[3];
        }
        return false;
    }

    uint64 GetData64(uint32 identifier)
    {
        if (identifier == DATA_GRANDMASTERVORPIL)
            return GrandmasterVorpil;

        return 0;
    }

    std::string GetSaveData()
    {
        OUT_SAVE_INST_DATA;

        std::ostringstream stream;
        stream << Encounter[0] << " ";
        stream << Encounter[1] << " ";
        stream << Encounter[2] << " ";
        stream << Encounter[3];

        OUT_SAVE_INST_DATA_COMPLETE;

        return stream.str();
    }

    void Load(const char* in)
    {
        if (!in)
        {
            OUT_LOAD_INST_DATA_FAIL;
            return;
        }

        OUT_LOAD_INST_DATA(in);

        std::istringstream loadStream(in);
        loadStream >> Encounter[0] >> Encounter[1] >> Encounter[2] >> Encounter[3];

        for(uint8 i = 0; i < ENCOUNTERS; ++i)
            if (Encounter[i] == IN_PROGRESS)
                Encounter[i] = NOT_STARTED;

        OUT_LOAD_INST_DATA_COMPLETE;
    }
};

InstanceData* GetInstanceData_instance_shadow_labyrinth(Map* map)
{
    return new instance_shadow_labyrinth(map);
}

/*######
## npc_tortured_skeleton
######*/

struct npc_tortured_skeletonAI : public ScriptedAI
{
    npc_tortured_skeletonAI(Creature* creature) : ScriptedAI(creature) {}

    void Reset()
    {
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
        me->SetStandState(UNIT_STAND_STATE_DEAD);
    }

    void EnterCombat(Unit* who)
    {
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
        me->SetStandState(UNIT_STAND_STATE_STAND);
    }

    void EnterEvadeMode()
    {
        me->RemoveAllAuras();
        me->DeleteThreatList();
        me->CombatStop();
        
        if (!me->IsAlive())
            return;    

        me->GetMotionMaster()->MoveTargetedHome();
    }

    void JustReachedHome()
    {
        Reset();
    }

    void UpdateAI(const uint32 diff)
    {
        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }
};

CreatureAI* GetAI_npc_tortured_skeleton(Creature* creature)
{
    return new npc_tortured_skeletonAI (creature);
}

void AddSC_instance_shadow_labyrinth()
{
    Script *newscript;
    newscript = new Script;
    newscript->Name = "instance_shadow_labyrinth";
    newscript->GetInstanceData = &GetInstanceData_instance_shadow_labyrinth;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_tortured_skeleton";
    newscript->GetAI = &GetAI_npc_tortured_skeleton;
    newscript->RegisterSelf();
}

