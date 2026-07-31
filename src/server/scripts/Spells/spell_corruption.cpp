/*
 * 2026 BFA-HavenCore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Scripts for the 8.3 corruption drawback penalties applied by
 * Player::UpdateCorruption from CorruptionEffects.db2.
 *
 * The magnitudes of these penalties exist in no static data. For every drawback
 * spell, SpellEffect.db2 carries BasePoints 0, Coefficient 0 and
 * RealPointsPerLevel 0, and the world DB override tables (spelleffect_dbc,
 * spell_dbc) hold no rows for them either. Retail computed the values
 * server-side and shipped the client only a placeholder - Blizzard's own 8.3
 * PTR tooltip read "slow your movement speed for 0 sec" for exactly this
 * reason. So these curves are documented approximations of the retail
 * behaviour described by the tooltips, not recovered data.
 */

#include "AreaTrigger.h"
#include "AreaTriggerAI.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "Unit.h"
#include "Util.h"

#include <algorithm>
#include <cmath>

enum CorruptionSpells
{
    SPELL_EYE_OF_CORRUPTION_SUMMON = 315154,
    SPELL_EYE_OF_CORRUPTION_DAMAGE = 315161,
    SPELL_GRAND_DELUSIONS_SUMMON   = 315186
};

// Grasping Tendrils, 1+ Corruption. Container 315175 procs on damage taken and
// triggers this 5 second snare.
//
// The curve below is confirmed at its primary source. Wowhead's 8.3 PTR article
// "Corrupted Items - Corruption Debuff Breakpoints and Scaling of Debuffs" (news
// 295810) states it outright - "The magnitude of the slow is equal to {Corruption +
// 10}" - and tabulates it from 10 corruption (20%) to 90 corruption (100%), which is
// exactly what these constants produce. The same article gives the proc rate as 1 RPPM
// and notes the debuff is magic and dispellable.
namespace GraspingTendrils
{
    constexpr float SlowBasePct     = 10.0f;  // snare at zero effective corruption
    constexpr float SlowPctPerPoint = 1.0f;   // added per point of effective corruption
    constexpr float SlowMaxPct      = 100.0f; // reported to reach a full root around 90
}

// Grasping Tendrils - 315176
class spell_corruption_grasping_tendrils : public AuraScript
{
    PrepareAuraScript(spell_corruption_grasping_tendrils);

    void CalculateAmount(AuraEffect const* /*aurEff*/, int32& amount, bool& canBeRecalculated)
    {
        Player* player = GetUnitOwner()->ToPlayer();
        if (!player)
            return;

        float const slow = std::min(GraspingTendrils::SlowBasePct + player->GetEffectiveCorruption() * GraspingTendrils::SlowPctPerPoint,
            GraspingTendrils::SlowMaxPct);

        // Unit::UpdateSpeed feeds the strongest negative modifier straight to AddPct,
        // so a snare has to be stored negative.
        amount = -int32(std::lround(slow));

        // Corruption can change while the 5s debuff is up - an item swap, a zone that
        // grants resistance. Snapshot at application instead of retuning mid-flight.
        canBeRecalculated = false;
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_corruption_grasping_tendrils::CalculateAmount, EFFECT_0, SPELL_AURA_MOD_DECREASE_SPEED);
    }
};

// Eye of Corruption, 20+ Corruption. Container 315169 procs on damage dealt - not
// damage taken - behind a 9 second category cooldown, and triggers 315154, which
// creates areatrigger 22815 three yards from the player for eight seconds. Blizzard's
// own tooltip on 315154 describes the whole mechanic:
//
//   "Your spells and abilities have a chance to summon an Eye of Corruption for $d.
//    The Eye inflicts increasing Shadow damage to you every $s2 sec while you remain
//    in range. Range and damage increase with further Corruption."
//
// The timing in that sentence is recoverable data and is read rather than hardcoded: $d
// is 8s (SpellDuration index 31), $s2 is 2 (315154 effect 1 BasePoints), and the school
// is Shadow (SpellMisc SchoolMask 32). The two quantities it promises scale with
// Corruption - "range and damage" - are the ones that are not in the client.
//
// The damage and the radius are not in the client at all. Every scaling field on 315161
// effect 0 is zero - EffectBasePoints, EffectBonusCoefficient, BonusCoefficientFromAP,
// Coefficient, Variance, EffectRealPointsPerLevel, ResourceCoefficient and
// EffectPointsPerResource alike - ContentTuningID is 0, and there is no hotfix row.
// Retail computed both server-side.
//
// Both are therefore taken from Wowhead's 8.3 PTR measurements, "Corrupted Items -
// Corruption Debuff Breakpoints and Scaling of Debuffs" (news 295810), which sampled the
// live values across corruption levels:
//
//   damage per tick   875 * Corruption - 1000      20 -> ~17.2k,  80 -> ~78.4k
//   radius (yards)    Corruption / 5               20 -> 4 yds,   80 -> 16 yds
//
// The article calls both approximate - its own damage table drifts a few percent above
// the formula past 50 corruption, partly because Inevitable Doom starts amplifying all
// damage taken and the article suspects it of double dipping. The stated formula is used
// rather than a curve fitted to the table, because the drift is a measurement artefact of
// another debuff rather than part of this one.
//
// This is a flat figure, not a fraction of health. That is a real difference in kind: the
// only corruption drawback that works off maximum health is Inescapable Consequences at
// 200. A flat value is also why the tier bites hardest on the squishiest targets, which
// is the behaviour the 8.3 forum reports describe.
//
// The tick can critically strike for 150%, which needs no code here - SetHitDamage runs
// before Spell::DoAllEffectOnTarget applies the crit roll, so the core does it.
namespace EyeOfCorruption
{
    constexpr float DamagePerCorruption = 875.0f;  // per point of effective corruption
    constexpr float DamageFlatOffset    = 1000.0f; // subtracted from the total
    constexpr float RadiusPerCorruption = 0.2f;    // yards per point, ie corruption / 5
    constexpr uint32 DefaultPeriod      = 2;       // seconds, if 315154 ever loses its effect 1
}

// Eye of Corruption - areatrigger 22815, created by 315154
struct at_corruption_eye_of_corruption : AreaTriggerAI
{
    at_corruption_eye_of_corruption(AreaTrigger* areatrigger) : AreaTriggerAI(areatrigger), _radius(0.0f)
    {
        uint32 period = EyeOfCorruption::DefaultPeriod;

        // The tick rate is data, not a constant: the client renders the same field as
        // "every $s2 sec", so reading it keeps the tooltip and the server in step.
        if (SpellInfo const* summon = sSpellMgr->GetSpellInfo(SPELL_EYE_OF_CORRUPTION_SUMMON))
            if (SpellEffectInfo const* periodEffect = summon->GetEffect(EFFECT_1))
                if (periodEffect->BasePoints > 0)
                    period = uint32(periodEffect->BasePoints);

        areatrigger->SetPeriodicProcTimer(period * IN_MILLISECONDS);
    }

    void OnCreate() override
    {
        _radius = CalculateRadius();

        TC_LOG_DEBUG("scripts.corruption", "Eye of Corruption: spawned, caster %s, radius %.2f",
            at->GetCasterGuid().ToString().c_str(), _radius);

        // The zone has to grow with corruption, and the trigger itself cannot: a cylinder
        // is searched against GetTemplate()->MaxSearchRadius (AreaTrigger.cpp:511), which
        // lives on the template every Eye shares, and this core has no per-instance scale
        // override to drive instead. AREATRIGGER_FLAG_HAS_DYNAMIC_SHAPE is not a way out -
        // AreaTriggerTemplate.h marks it "Implemented for Spheres" and 22815 is a cylinder.
        //
        // So the radius is kept here and tested directly in OnPeriodicProc, and the visual
        // is scaled by the same ratio so the graphic still shows where the zone ends. That
        // matters more than it sounds: at 80 corruption the zone is 16 yards against the
        // template's 5, and an unscaled graphic would leave the player no way to see it.
        if (_radius > 0.0f)
            if (float templateRadius = at->GetTemplate()->CylinderDatas.Radius)
                at->SetObjectScale(_radius / templateRadius);
    }

    void OnPeriodicProc() override
    {
        Unit* caster = at->GetCaster();
        if (!caster)
            return;

        // A radius of zero used to end the tick here, which meant one bad reading at
        // creation silenced the Eye for its whole eight seconds. OnCreate can legitimately
        // come up empty - the caster may not resolve, or corruption reads 0 because a tier
        // aura outlived the gear that granted it - so retry instead of staying inert, and
        // fall back to the trigger's own cylinder, which is the test this script used
        // before the radius was made to scale.
        if (_radius <= 0.0f)
            _radius = CalculateRadius();

        // The Eye only ever hits the player who summoned it, so there is no target list to
        // walk - "while you remain in range" is just a distance test on that one player.
        // Deliberately 2d: the zone is a cylinder, so height is not part of the test.
        //
        // Measured centre to centre, and not with IsWithinDist2d, which expands to
        // IsInDist2d(pos, dist + GetObjectSize()) and so quietly adds the player's
        // CombatReach to whatever radius it is handed (Object.cpp:1100). That made the
        // damage zone larger than the ring drawn for it - by a yard and a half on a small
        // race and several on a large one - and the player was hit outside the visible
        // edge. The graphic is scaled to exactly _radius below, so the test must be too.
        bool const inRange = _radius > 0.0f
            ? caster->GetExactDist2d(at) <= _radius
            : at->GetInsideUnits().count(caster->GetGUID()) != 0;

        TC_LOG_DEBUG("scripts.corruption", "Eye of Corruption: tick, radius %.2f, dist %.2f, in range %u",
            _radius, caster->GetExactDist2d(at), uint32(inRange));

        if (inRange)
            caster->CastSpell(caster, SPELL_EYE_OF_CORRUPTION_DAMAGE, true);
    }

private:
    float CalculateRadius() const
    {
        Player* player = at->GetCaster() ? at->GetCaster()->ToPlayer() : nullptr;
        return player ? player->GetEffectiveCorruption() * EyeOfCorruption::RadiusPerCorruption : 0.0f;
    }

    float _radius;
};

// Eye of Corruption - 315161
class spell_corruption_eye_of_corruption : public SpellScript
{
    PrepareSpellScript(spell_corruption_eye_of_corruption);

    void CalculateDamage(SpellEffIndex /*effIndex*/)
    {
        Unit* target = GetHitUnit();
        Unit* caster = GetCaster();
        if (!target || !caster)
            return;

        Player* player = caster->ToPlayer();
        if (!player)
            return;

        float damage = EyeOfCorruption::DamagePerCorruption * player->GetEffectiveCorruption() - EyeOfCorruption::DamageFlatOffset;

        // Below about 1.15 corruption the formula is still negative. The tier only unlocks
        // at 20, so this never fires in practice, but a negative SetHitDamage would heal.
        if (damage <= 0.0f)
            return;

        // "increasing Shadow damage": every tick leaves a stack behind that raises what
        // the next one lands for. Effect 1 holds the per-stack figure and the client
        // renders it from that same row as "Damage taken from Eye of Corruption
        // increased by $s2%", so it is read rather than repeated here. The stack this
        // cast applies is not counted - effect 1 has not run yet at effect 0's hook,
        // which is what makes the first tick land unamplified.
        if (SpellEffectInfo const* stackEffect = GetSpellInfo()->GetEffect(EFFECT_1))
            AddPct(damage, float(stackEffect->BasePoints) * float(target->GetAuraCount(GetSpellInfo()->Id)));

        SetHitDamage(int32(damage));
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_corruption_eye_of_corruption::CalculateDamage, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

// Grand Delusions, 40+ Corruption. Container 315184 procs on damage taken and triggers
// 315186, whose single effect summons creature 161895, the Thing From Beyond.
//
// The client supplies more of this tier than of the others, and it is worth being precise
// about which parts are data and which are not:
//
//   - 315186 carries DurationIndex 31, so the summon lives 8 seconds. Wowhead's article
//     says "pursues for 10 sec"; that was measured on the PTR, and where the client is
//     explicit the client wins, exactly as the thresholds do.
//   - Its summon effect uses radius index 9, so the Thing appears up to 20 yards away.
//   - SummonProperties 4793 sets Control NONE and Faction 14, so TemporarySummon::InitStats
//     overrides the friendly faction 35 on the creature template and the Thing spawns
//     hostile. It also carries SUMMON_PROP_FLAG_PERSONAL_SPAWN, so only the summoner ever
//     sees their own Thing.
//
// What the client does not supply is the pursuit speed or the damage on contact. The
// article gives no formula for either - only that the speed scales with corruption and
// that a hit deals "about your health". Both constants below are therefore documented
// approximations, in the same sense as the header note at the top of this file, and both
// are isolated so they can be retuned without touching the logic.
namespace GrandDelusions
{
    constexpr float Threshold             = 40.0f;   // CorruptionEffects.db2 MinCorruption
    constexpr float SpeedRateAtThreshold  = 0.75f;   // 5.25 yd/s against a player's 7.0
    constexpr float SpeedRatePerPoint     = 0.0125f; // reaches parity at 60, overtakes above
    constexpr float SpeedRateMax          = 2.0f;
    constexpr float DamagePctOfMaxHealth  = 100.0f;  // "about your health"
}

// Thing From Beyond - creature 161895, summoned by 315186
struct npc_corruption_thing_from_beyond : ScriptedAI
{
    npc_corruption_thing_from_beyond(Creature* creature) : ScriptedAI(creature) { }

    void IsSummonedBy(Unit* summoner) override
    {
        Player* player = summoner ? summoner->ToPlayer() : nullptr;
        if (!player)
        {
            me->DespawnOrUnsummon();
            return;
        }

        _summonerGuid = player->GetGUID();

        // The Thing is a movement puzzle, not a fight: it chases one player and bursts on
        // contact. Passive keeps it from picking up an ordinary melee swing on the way,
        // which would both pre-damage the target and drag it into normal combat and evade
        // handling. The player can still kill it - it is hostile, just not an attacker.
        me->SetReactState(REACT_PASSIVE);

        float const corruption = player->GetEffectiveCorruption();
        float const rate = std::min(GrandDelusions::SpeedRateMax,
            GrandDelusions::SpeedRateAtThreshold
                + std::max(0.0f, corruption - GrandDelusions::Threshold) * GrandDelusions::SpeedRatePerPoint);

        TC_LOG_DEBUG("scripts.corruption", "Thing From Beyond: spawned for %s at corruption %.1f, "
            "speed rate %.2f, %.1f yards away",
            _summonerGuid.ToString().c_str(), corruption, rate, me->GetExactDist2d(player));

        me->SetSpeedRate(MOVE_RUN, rate);
        me->GetMotionMaster()->MoveChase(player);
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        if (_summonerGuid.IsEmpty())
            return;

        Player* player = ObjectAccessor::GetPlayer(*me, _summonerGuid);
        if (!player || !player->IsAlive())
        {
            me->DespawnOrUnsummon();
            return;
        }

        if (!me->IsWithinMeleeRange(player))
            return;

        // No spell exists for this hit. The whole client-side chain is 315184 -> 315186 ->
        // summon, and 315186 has exactly one effect, so retail drove the contact damage from
        // creature data the client never shipped. Dealing it directly is the honest stand-in;
        // if the real spell ever turns up, this becomes a CastSpell and the magnitude moves
        // into the script for that spell.
        uint32 const damage = CalculatePct(player->GetMaxHealth(), GrandDelusions::DamagePctOfMaxHealth);

        TC_LOG_DEBUG("scripts.corruption", "Thing From Beyond: reached %s, dealing %u of %u max health",
            _summonerGuid.ToString().c_str(), damage, uint32(player->GetMaxHealth()));

        me->DealDamage(player, damage, nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_SHADOW);

        // One hit and it is spent - it does not linger to hit again inside its 8 seconds.
        me->DespawnOrUnsummon();
    }

private:
    ObjectGuid _summonerGuid;
};

void AddSC_corruption_spell_scripts()
{
    RegisterAuraScript(spell_corruption_grasping_tendrils);
    RegisterSpellScript(spell_corruption_eye_of_corruption);
    RegisterAreaTriggerAI(at_corruption_eye_of_corruption);
    RegisterCreatureAI(npc_corruption_thing_from_beyond);
}
