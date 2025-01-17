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
SDName: Boss_Drakkisath
SD%Complete: 100
SDComment:
SDCategory: Blackrock Spire
EndScriptData */

#include "precompiled.h"

#define SPELL_FIRENOVA                  23462
#define SPELL_CLEAVE                    20691
#define SPELL_CONFLIGURATION            16805
#define SPELL_THUNDERCLAP               15548               //Not sure if right ID. 23931 would be a harder possibility.

struct boss_drakkisathAI : public ScriptedAI
{
    boss_drakkisathAI(Creature *c) : ScriptedAI(c) {}

    int32 FireNova_Timer;
    int32 Cleave_Timer;
    int32 Confliguration_Timer;
    int32 Thunderclap_Timer;

    void Reset()
    {
        FireNova_Timer = 6000;
        Cleave_Timer = 8000;
        Confliguration_Timer = 15000;
        Thunderclap_Timer = 17000;
    }

    void EnterCombat(Unit *who)
    {
    }

    void UpdateAI(const uint32 diff)
    {
        //Return since we have no target
        if (!UpdateVictim() )
            return;

        FireNova_Timer -= diff;
        if (FireNova_Timer <= diff)
        {
            DoCast(m_creature->GetVictim(),SPELL_FIRENOVA);
            FireNova_Timer += 10000;
        }
        

        Cleave_Timer -= diff;
        if (Cleave_Timer <= diff)
        {
            DoCast(m_creature->GetVictim(),SPELL_CLEAVE);
            Cleave_Timer += 8000;
        }
        

        Confliguration_Timer -= diff;
        if (Confliguration_Timer <= diff)
        {
            DoCast(m_creature->GetVictim(),SPELL_CONFLIGURATION);
            Confliguration_Timer += 18000;
        }
        

        Thunderclap_Timer -= diff;
        if (Thunderclap_Timer <= diff)
        {
            DoCast(m_creature->GetVictim(),SPELL_THUNDERCLAP);
            Thunderclap_Timer += 20000;
        }
        

        DoMeleeAttackIfReady();
    }
};
CreatureAI* GetAI_boss_drakkisath(Creature *_Creature)
{
    return new boss_drakkisathAI (_Creature);
}

void AddSC_boss_drakkisath()
{
    Script *newscript;
    newscript = new Script;
    newscript->Name="boss_drakkisath";
    newscript->GetAI = &GetAI_boss_drakkisath;
    newscript->RegisterSelf();
}

