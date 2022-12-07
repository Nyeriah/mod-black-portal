/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "Config.h"
#include "Chat.h"
#include "TaskScheduler.h"

enum Texts
{
    SAY_INTRO   = 0,
    SAY_KILL    = 1,
    SAY_DESPAWN = 2
};

enum Spells
{
    SPELL_SHADOW_VOLLEY = 30354,
    SPELL_CLEAVE = 31779,
    SPELL_THUNDERCLAP = 36706,
    SPELL_VOID_BOLT = 21066,
    SPELL_MARK_OF_KAZZAK = 21056,
    SPELL_MARK_OF_KAZZAK_DAMAGE = 21058,
    SPELL_ENRAGE = 32964,
    SPELL_CAPTURE_SOUL = 32966,
    SPELL_TWISTED_REFLECTION = 21063,

    //
    SPELL_SUMMON_FLAMES = 19629,
};

enum Events
{
    EVENT_SHADOW_VOLLEY = 1,
    EVENT_CLEAVE = 2,
    EVENT_THUNDERCLAP = 3,
    EVENT_VOID_BOLT = 4,
    EVENT_MARK_OF_KAZZAK = 5,
    EVENT_ENRAGE = 6,
    EVENT_TWISTED_REFLECTION = 7,
    EVENT_BERSERK = 8
};

enum Phases
{
    PHASE_NORMAL = 1,
    PHASE_OUTRO = 2
};

enum Misc
{
    GO_CONSUMING_FLAMES = 178672,
    NPC_MASSIVE_INFERNAL = 8680,
    NPC_INFERNAL_HOUND = 19207
};

class boss_highlord_kruul : public CreatureScript
{
public:
    boss_highlord_kruul() : CreatureScript("boss_highlord_kruul") { }

    struct boss_highlord_kruulAI : public ScriptedAI
    {
        boss_highlord_kruulAI(Creature* creature) : ScriptedAI(creature)
        {
            _supremeMode = false;
            _intro = false;
        }

        void Reset() override
        {
            _events.Reset();
            _supremeMode = false;

            if (!sConfigMgr->GetOption<bool>("ModBlackPortal.Enable", true))
            {
                me->DisappearAndDie();
                return;
            }

            if (!_intro)
            {
                Talk(SAY_INTRO);

                me->SummonCreature(NPC_MASSIVE_INFERNAL, me->GetNearPosition(15.0f, 1.0f));
                me->SummonCreature(NPC_MASSIVE_INFERNAL, me->GetNearPosition(15.0f, -1.0f));

                Position pos = me->GetNearPosition(15.0f, 1.0f);

                if (GameObject* fires = me->SummonGameObject(GO_CONSUMING_FLAMES, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2 * HOUR * IN_MILLISECONDS))
                {
                    _gobGuids.insert(fires->GetGUID());
                }
                pos = me->GetNearPosition(15.0f, -1.0f);

                if (GameObject* fires = me->SummonGameObject(GO_CONSUMING_FLAMES, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2 * HOUR * IN_MILLISECONDS))
                {
                    _gobGuids.insert(fires->GetGUID());
                }

                me->GetMotionMaster()->MoveRandom(10.0f);
                _intro = true;

                _scheduler.CancelAll();
                _scheduler.Schedule(2h, [this](TaskContext context)
                {
                    if (!me->IsInCombat())
                    {
                        Talk(SAY_DESPAWN);
                        DespawnMinionsAndFlames();
                    }
                    else
                    {
                        context.Repeat(5s);
                    }
                }).Schedule(10s, [this](TaskContext context)
                {
                    if (!me->IsInCombat())
                    {
                        DoCastAOE(SPELL_SUMMON_FLAMES);
                    }
                    context.Repeat();
                }).Schedule(10s, [this](TaskContext context)
                {
                    me->SummonCreature(NPC_INFERNAL_HOUND, me->GetRandomNearPosition(15.0f), TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 5 * MINUTE * IN_MILLISECONDS);
                    context.Repeat(1min);
                });
            }
        }

        void DespawnMinionsAndFlames()
        {
            for (ObjectGuid guid : _minionGuids)
            {
                if (Creature* minion = ObjectAccessor::GetCreature(*me, guid))
                {
                    minion->DespawnOrUnsummon();
                }
            }

            for (ObjectGuid guid : _gobGuids)
            {
                if (GameObject* flames = ObjectAccessor::GetGameObject(*me, guid))
                {
                    flames->DespawnOrUnsummon();
                }
            }
        }

        void JustSummoned(Creature* summon) override
        {
            _minionGuids.insert(summon->GetGUID());
            summon->SetFaction(me->GetFaction());
        }

        void EnterCombat(Unit* /*who*/) override
        {
            _events.ScheduleEvent(EVENT_SHADOW_VOLLEY, urand(6000, 10000));
            _events.ScheduleEvent(EVENT_CLEAVE, 7000);
            _events.ScheduleEvent(EVENT_THUNDERCLAP, urand(14000, 18000));
            _events.ScheduleEvent(EVENT_VOID_BOLT, 30000);
            _events.ScheduleEvent(EVENT_MARK_OF_KAZZAK, 25000);
            _events.ScheduleEvent(EVENT_TWISTED_REFLECTION, 33000);
            _events.ScheduleEvent(EVENT_BERSERK, 60000);
        }

        void KilledUnit(Unit* victim) override
        {
            if (victim->IsPlayer())
            {
                DoCast(me, SPELL_CAPTURE_SOUL);
                Talk(SAY_KILL, victim);
            }
        }

        void UpdateAI(uint32 diff) override
        {
            _scheduler.Update(diff);
            // Return since we have no target
            if (!UpdateVictim())
                return;

            _events.Update(diff);

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            while (uint32 eventId = _events.ExecuteEvent())
            {
                switch (eventId)
                {
                case EVENT_SHADOW_VOLLEY:
                    DoCastVictim(SPELL_SHADOW_VOLLEY);
                    if (!_supremeMode)
                        _events.ScheduleEvent(EVENT_SHADOW_VOLLEY, urand(4000, 30000));
                    else
                        _events.ScheduleEvent(EVENT_SHADOW_VOLLEY, 1000);
                    break;
                case EVENT_CLEAVE:
                    DoCastVictim(SPELL_CLEAVE);
                    _events.ScheduleEvent(EVENT_CLEAVE, urand(8000, 12000));
                    break;
                case EVENT_THUNDERCLAP:
                    DoCastVictim(SPELL_THUNDERCLAP);
                    _events.ScheduleEvent(EVENT_THUNDERCLAP, urand(10000, 14000));
                    break;
                case EVENT_VOID_BOLT:
                    DoCastVictim(SPELL_VOID_BOLT);
                    _events.ScheduleEvent(EVENT_VOID_BOLT, urand(15000, 18000));
                    break;
                case EVENT_MARK_OF_KAZZAK:
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1, [&](Unit* u) { return u && !u->IsPet() && u->getPowerType() == POWER_MANA; }))
                        DoCast(target, SPELL_MARK_OF_KAZZAK);
                    _events.ScheduleEvent(EVENT_MARK_OF_KAZZAK, 20000);
                    break;
                case EVENT_ENRAGE:
                    DoCast(me, SPELL_ENRAGE);
                    _events.ScheduleEvent(EVENT_ENRAGE, 60000);
                    break;
                case EVENT_TWISTED_REFLECTION:
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1, 0.0f, true))
                        DoCast(target, SPELL_TWISTED_REFLECTION);
                    _events.ScheduleEvent(EVENT_TWISTED_REFLECTION, 15000);
                    break;
                case EVENT_BERSERK:
                    _supremeMode = true;
                    _events.ScheduleEvent(EVENT_SHADOW_VOLLEY, 1000);
                    break;
                default:
                    break;
                }
            }

            DoMeleeAttackIfReady();
        }

        void JustDied(Unit* /*killer*/) override
        {
            _intro = false;
            DespawnMinionsAndFlames();
            _scheduler.CancelAll();
        }

    private:
        GuidSet _minionGuids;
        GuidSet _gobGuids;
        EventMap _events;
        TaskScheduler _scheduler;
        bool _supremeMode;
        bool _intro;
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new boss_highlord_kruulAI(creature);
    }
};

// Add all scripts in one
void AddModBlackPortalScripts()
{
    new boss_highlord_kruul();
}
