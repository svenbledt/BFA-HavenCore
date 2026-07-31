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
#include "Player.h"
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
    SPELL_EYE_OF_CORRUPTION_DAMAGE = 315161
};

// Grasping Tendrils, 1+ Corruption. Container 315175 procs on damage taken and
// triggers this 5 second snare.
//
// The curve below is "slow% = corruption + 10", reported secondhand from Wowhead's
// 8.3 PTR article "Corruption Debuff Breakpoints and Scaling of Debuffs". That
// article's body renders client-side and is not recoverable from the live page or
// the Wayback snapshot, so this could not be confirmed at its primary source.
// It is used because it fits what can be checked: the tier unlocks at 1 Corruption,
// where a purely multiplicative curve would land on ~0% and make the tier inert at
// its own threshold, and a flat 1% per point matches a tooltip that promises smooth
// growth with no breakpoints. Treat the three constants as tunable, not as gospel.
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
// Everything in that sentence except the damage number is recoverable data, and is
// read from it below rather than hardcoded: $d is 8s (SpellDuration index 31), $s2 is
// 2 (315154 effect 1 BasePoints), the radius is 5 yards (areatrigger_template 22815
// CylinderDatas.Radius), and the school is Shadow (SpellMisc SchoolMask 32).
//
// Two things are deliberately not implemented:
//
// "Range ... increase[s] with further Corruption" - template 22815 sets
// AREATRIGGER_FLAG_HAS_DYNAMIC_SHAPE, which is the mechanism retail grew the Eye with,
// but AreaTriggerTemplate.h flags it "Implemented for Spheres" and 22815 is a cylinder.
// Growing only the damage radius would leave a hitbox wider than the visual, which is
// worse than a fixed radius, so the Eye keeps the template's 5 yards.
//
// The damage per tick - like every other corruption magnitude, 315161 effect 0 carries
// BasePoints 0, and here ContentTuningID is 0 too, so retail did not even derive it
// from content tuning. The curve below is invented to fit the tooltip, not recovered.
namespace EyeOfCorruption
{
    constexpr float DamageBasePct     = 2.0f;   // of max health per tick at zero corruption
    constexpr float DamagePctPerPoint = 0.05f;  // added per point of effective corruption
    constexpr float DamageMaxPct      = 10.0f;  // ceiling on one tick before stacks
    constexpr uint32 DefaultPeriod    = 2;      // seconds, if 315154 ever loses its effect 1
}

// Eye of Corruption - areatrigger 22815, created by 315154
struct at_corruption_eye_of_corruption : AreaTriggerAI
{
    at_corruption_eye_of_corruption(AreaTrigger* areatrigger) : AreaTriggerAI(areatrigger)
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

    void OnPeriodicProc() override
    {
        Unit* caster = at->GetCaster();
        if (!caster)
            return;

        // The Eye only ever hits the player who summoned it, so there is no target list
        // to walk - "while you remain in range" is just a test of whether that player is
        // still standing in the cylinder.
        if (at->GetInsideUnits().count(caster->GetGUID()))
            caster->CastSpell(caster, SPELL_EYE_OF_CORRUPTION_DAMAGE, true);
    }
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

        float const pct = std::min(EyeOfCorruption::DamageBasePct + player->GetEffectiveCorruption() * EyeOfCorruption::DamagePctPerPoint,
            EyeOfCorruption::DamageMaxPct);

        float damage = CalculatePct(float(target->GetMaxHealth()), pct);

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

void AddSC_corruption_spell_scripts()
{
    RegisterAuraScript(spell_corruption_grasping_tendrils);
    RegisterSpellScript(spell_corruption_eye_of_corruption);
    RegisterAreaTriggerAI(at_corruption_eye_of_corruption);
}
