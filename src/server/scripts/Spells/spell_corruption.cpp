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
    constexpr float DefaultRadius       = 5.0f;    // yards, if 22815 ever loses its shape data
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
        // "The size and damage of the corrupted zone increase with higher Corruption", and the
        // article gives the size the same treatment as the damage: radius = Corruption / 5,
        // tabulated 20 -> 4 yards through 80 -> 16. It also says how retail achieved it - the
        // zone "just increase[s] the size of a graphic that isn't supposed to be large" - so
        // the drawn ring is the base shape rescaled, not a different shape.
        //
        // 22815 is that base shape: a cylinder sniffed off retail (VerifiedBuild 34220) with
        // Radius 5, which is the size the client draws with no override, and which lines up
        // with corruption 25. Everything else is that radius times a scale.
        //
        // Both numbers below come off the same division, which is the point. An earlier
        // version scaled the radius but drove the graphic with SetObjectScale, and the two did
        // not move together - the damage cut outside the ring the player could see.
        // SetOverrideScaleCurve is the field the client actually resizes an areatrigger from,
        // so the ring edge and the range test are now the same edge by construction.
        float const templateRadius = at->GetTemplate()->CylinderDatas.Radius > 0.0f
            ? at->GetTemplate()->CylinderDatas.Radius
            : EyeOfCorruption::DefaultRadius;

        _radius = templateRadius;

        if (Player* player = at->GetCaster() ? at->GetCaster()->ToPlayer() : nullptr)
        {
            // A zero reading is possible - a tier aura can outlive the gear that granted it -
            // and it must not shrink the zone to nothing. Falling back to the drawn shape
            // leaves the Eye behaving exactly as it does with no scaling applied.
            float const scaled = player->GetEffectiveCorruption() * EyeOfCorruption::RadiusPerCorruption;
            if (scaled > 0.0f)
                _radius = scaled;
        }

        at->SetOverrideScaleCurve(_radius / templateRadius);

        TC_LOG_DEBUG("scripts.corruption", "Eye of Corruption: spawned, caster %s, radius %.2f "
            "(template %.2f, scale %.2f)",
            at->GetCasterGuid().ToString().c_str(), _radius, templateRadius, _radius / templateRadius);
    }

    void OnPeriodicProc() override
    {
        Unit* caster = at->GetCaster();
        if (!caster)
            return;

        // The Eye only ever hits the player who summoned it, so there is no target list to
        // walk - "while you remain in range" is just a distance test on that one player.
        // Deliberately 2d: the zone is a cylinder, so height is not part of the test.
        //
        // Measured centre to centre. Neither IsWithinDist2d nor the trigger's own
        // GetInsideUnits() will do, and for the same reason: both end up in IsInDist, which
        // adds the player's GetObjectSize() to the radius it was handed (Object.cpp:1100).
        // That pads the damage zone past the drawn ring by the player's CombatReach - a yard
        // and a half on a small race, several on a large one - so the player takes damage
        // standing outside the edge they can see.
        bool const inRange = caster->GetExactDist2d(at) <= _radius;

        TC_LOG_DEBUG("scripts.corruption", "Eye of Corruption: tick, radius %.2f, dist %.2f, in range %u",
            _radius, caster->GetExactDist2d(at), uint32(inRange));

        if (inRange)
            caster->CastSpell(caster, SPELL_EYE_OF_CORRUPTION_DAMAGE, true);
    }

private:
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
//   - creature_template leaves BaseAttackTime 0, which ObjectMgr.cpp:1093 turns into the
//     2 second default, so the Thing has a swing timer to strike on.
//
// What the client does not supply is the pursuit speed or the damage. The article gives no
// formula for either - only that the speed scales with corruption, and that the Thing
// "reaching you deals about your health in damage", which the author never saw happen and
// reports second-hand. Both constants below are therefore documented approximations, in the
// same sense as the header note at the top of this file, and both are isolated so they can be
// retuned without touching the logic.
namespace GrandDelusions
{
    constexpr uint32 ContainerSpell       = 315184;  // the debuff the player carries
    constexpr uint32 CloneCasterSpell     = 60352;   // generic Clone Caster, see IsSummonedBy
    constexpr float Threshold             = 40.0f;   // CorruptionEffects.db2 MinCorruption

    // The look, taken from the client rather than invented. SpellXSpellVisual points 315186 at
    // SpellVisual 94321, and SpellVisualEvent gives that visual exactly two entries: kit 122670
    // at offset 0 and kit 122671 at offset 1000, both on the target. Those two kits are shared
    // with SpellVisual 93517, which belongs to 306955 "Madness: Dark Delusions" - the Horrific
    // Vision spell that summons Dark Delusion (creature 157425) to chase you the same way. Two
    // different encounters reusing one pair of kits is what identifies them as the pursuer
    // effect itself rather than anything specific to a cast, so replaying them on the summon is
    // reproducing retail's own data, not approximating it.
    //
    // The server has to send them. A summon spell's visual plays on the cast - source is the
    // player, target is the summon destination - and nothing in that chain reaches the creature
    // that the cast produced, so a client left to itself draws the clone with no effect on it.
    // That is exactly what was reported: the right body, none of the colour.
    constexpr uint32 VisualKitSpawn       = 122670;
    constexpr uint32 VisualKitSustain     = 122671;
    constexpr uint32 VisualKitSustainDelay = 1000;   // SpellVisualEvent offset for the second kit

    constexpr float SpeedRateAtThreshold  = 0.75f;   // 5.25 yd/s against a player's 7.0
    constexpr float SpeedRatePerPoint     = 0.0125f; // reaches parity at 60, overtakes above
    constexpr float SpeedRateMax          = 2.0f;

    // "About your health" is what a full pursuit costs you, not what one swing does. The Thing
    // strikes on its own attack speed for as long as it stays on you, so the total is divided
    // across the strikes it can land in its lifetime, and being caught late costs less than
    // being caught at once. Both halves of that division are read from the summon - only the
    // total is a constant here.
    constexpr float TotalDamagePctOfMaxHealth = 90.0f;
}

// Thing From Beyond - creature 161895, summoned by 315186
struct npc_corruption_thing_from_beyond : ScriptedAI
{
    npc_corruption_thing_from_beyond(Creature* creature) : ScriptedAI(creature),
        _strikeCooldown(0), _sustainVisualTimer(0), _chaseReportTimer(0),
        _damagePctPerStrike(GrandDelusions::TotalDamagePctOfMaxHealth) { }

    void IsSummonedBy(Unit* summoner) override
    {
        Player* player = summoner ? summoner->ToPlayer() : nullptr;
        if (!player)
        {
            me->DespawnOrUnsummon();
            return;
        }

        _summonerGuid = player->GetGUID();

        // Wear the summoner's face. The only creature_template_model row for 161895 is display
        // 11686 - the invisible stalker that 8467 other templates in world use for bunnies and
        // kill credit - and that is not an oversight to be routed around: retail's Thing From
        // Beyond is a copy of the player it is chasing, so the model was never meant to come
        // from the template. An earlier version repointed the summon at 160966, a level 1
        // variant carrying display 92610, which drew something but drew the wrong thing.
        //
        // The clone has to be a real aura, not just a display id. WorldSession::
        // HandleMirrorImageDataRequest (SpellHandler.cpp:534) answers the client's request for
        // the copy's gear and features only if the unit actually carries SPELL_AURA_CLONE_CASTER,
        // and it reads the appearance off that aura's caster - so the player must be the caster,
        // and setting UNIT_FLAG2_MIRROR_IMAGE by hand would leave the client asking a question
        // nothing answers. 60352 is the core's generic Clone Caster: one effect, aura 247, no
        // duration, no script attached, already used this way by mage Mirror Image
        // (spell_mage.cpp:2638) and the Amalgam of Souls echoes.
        //
        // It is applied rather than cast, and that part is not a shortcut. Clone Caster is a
        // positive aura and this summon is hostile - SummonProperties 4793 sets faction 14 over
        // the template's friendly 35 - so CheckCast answers SPELL_FAILED_BAD_TARGETS. Spell.cpp
        // :3075 forgives exactly that result, but only under TRIGGERED_IGNORE_TARGET_CHECK,
        // which is 0x00100000 and therefore outside TRIGGERED_FULL_MASK's 0x0007FFFF: the header
        // files it under "debug flags (used with .cast triggered commands)", so CastSpell(...,
        // true) cannot reach it and .cast 60352 triggered can, which is why the two disagree.
        // AddAura skips CheckCast entirely and screens only for immunity, and 161895 carries
        // mechanic_immune_mask 0 with every unit_flags field at 0, so nothing there can refuse it.
        //
        // Nothing in the corruption chain does this - 315184 is a bare proc trigger and 315186 a
        // bare summon - so retail drove it from creature data the client never shipped, the same
        // as the contact damage below.
        player->AddAura(GrandDelusions::CloneCasterSpell, me);

        // Level the Thing to its summoner. GetEffectiveResistChance adds
        // (victim level - attacker level) * 5 resistance (Unit.cpp:1861), so a Thing below its
        // target's level has half its damage resisted away before it lands - at level 1 against
        // a level 120 player that is 595 resistance over a constant of 600, an average resist of
        // 49.8%, which is what the damage log showed while the summon was still repointed at
        // 160966. 161895 is level 120 and so already correct at the cap, but a mirror of the
        // player should be the player's level at any level, and matching them zeroes the term in
        // both directions. Players carry no resistance of their own in this expansion, so the
        // hit then lands whole.
        me->SetLevel(player->getLevel());

        // Spread the pursuit's total across the strikes it has time for. Both numbers are the
        // summon's own: GetBaseAttackTime is the creature's swing timer (creature_template
        // leaves it 0, which ObjectMgr.cpp:1093 turns into the 2 second default), and
        // TempSummon::GetTimer is its remaining life, still the full 8 seconds here because
        // InitSummon runs immediately after InitStats set it.
        uint32 const swingTime = me->GetBaseAttackTime(BASE_ATTACK);
        uint32 const pursuitTime = me->ToTempSummon() ? me->ToTempSummon()->GetTimer() : 0;
        uint32 const strikes = (swingTime && pursuitTime) ? std::max(1u, pursuitTime / swingTime) : 1;

        _damagePctPerStrike = GrandDelusions::TotalDamagePctOfMaxHealth / float(strikes);

        // The Thing is a movement puzzle, not a fight: it chases one player and strikes for as
        // long as it can stay on them. Passive keeps the core's own melee out of it, which
        // would otherwise land ordinary swings alongside the scripted ones and drag the Thing
        // into normal combat and evade handling. The player can still kill it - it is hostile,
        // just not an attacker in the core's sense.
        me->SetReactState(REACT_PASSIVE);

        float const corruption = player->GetEffectiveCorruption();
        float const rate = std::min(GrandDelusions::SpeedRateMax,
            GrandDelusions::SpeedRateAtThreshold
                + std::max(0.0f, corruption - GrandDelusions::Threshold) * GrandDelusions::SpeedRatePerPoint);

        // Report whether the clone took, rather than leaving it to be judged by eye. A cast that
        // fails CheckCast is silent, and the failure looks identical to a working clone of an
        // invisible player - so the log carries the two facts that separate them: the aura is on
        // the Thing, and its display id is now the player's rather than the template's 11686.
        TC_LOG_DEBUG("scripts.corruption", "Thing From Beyond: spawned for %s at corruption %.1f, "
            "speed rate %.2f, %.1f yards away, clone %s (display %u, player %u, native %u)",
            _summonerGuid.ToString().c_str(), corruption, rate, me->GetExactDist2d(player),
            me->HasAuraType(SPELL_AURA_CLONE_CASTER) ? "applied" : "MISSING",
            me->GetDisplayId(), player->GetDisplayId(), me->GetNativeDisplayId());

        // Dress the clone. The kits are the ones SpellVisual 94321 would have run had the
        // client been told to run them on this creature; the second one follows a second
        // later because that is the offset SpellVisualEvent records for it. The first is
        // given the pursuit's own length as its duration so the effect lasts exactly as long
        // as the Thing does rather than being cut to a default.
        me->SendPlaySpellVisualKit(GrandDelusions::VisualKitSpawn, 0, pursuitTime);
        _sustainVisualTimer = GrandDelusions::VisualKitSustainDelay;

        me->SetSpeedRate(MOVE_RUN, rate);
        me->GetMotionMaster()->MoveChase(player);
    }

    void UpdateAI(uint32 diff) override
    {
        if (_summonerGuid.IsEmpty())
            return;

        Player* player = ObjectAccessor::GetPlayer(*me, _summonerGuid);
        if (!player || !player->IsAlive())
        {
            me->DespawnOrUnsummon();
            return;
        }

        // Second half of the summon's visual, at the offset SpellVisualEvent gives it.
        if (_sustainVisualTimer)
        {
            if (_sustainVisualTimer <= diff)
            {
                me->SendPlaySpellVisualKit(GrandDelusions::VisualKitSustain, 0, 0);
                _sustainVisualTimer = 0;
            }
            else
                _sustainVisualTimer -= diff;
        }

        // A pursuit that arrives and then stands there is indistinguishable, from outside, from
        // one that never arrived: the report was "it run to me and then stand still without
        // hitting me". Every quantity the strike depends on is printed once a second so the next
        // run says which one is wrong instead of leaving it to be guessed. IsWithinMeleeRange
        // measures in three dimensions while the chase generator stops on a two dimensional
        // check, so the height difference is carried too - that gap is the one way the two can
        // disagree about whether the Thing is close enough.
        if (_chaseReportTimer <= diff)
        {
            TC_LOG_DEBUG("scripts.corruption", "Thing From Beyond: dist %.2f (2d %.2f, dz %.2f), "
                "melee range %.2f, reach %.2f/%.2f, movegen %u, moving %u, evade %u, combat %u, "
                "strike cooldown %u, run speed %.2f",
                me->GetExactDist(player), me->GetExactDist2d(player),
                me->GetPositionZ() - player->GetPositionZ(), me->GetMeleeRange(player),
                me->GetCombatReach(), player->GetCombatReach(),
                uint32(me->GetMotionMaster()->GetCurrentMovementGeneratorType()),
                uint32(me->isMoving()), uint32(me->IsInEvadeMode()), uint32(me->IsInCombat()),
                _strikeCooldown, me->GetSpeed(MOVE_RUN));
            _chaseReportTimer = 1000;
        }
        else
            _chaseReportTimer -= diff;

        if (_strikeCooldown > diff)
        {
            _strikeCooldown -= diff;
            return;
        }

        if (!me->IsWithinMeleeRange(player))
            return;

        // No spell exists for this hit. The whole client-side chain is 315184 -> 315186 ->
        // summon; 315186 has exactly one effect, neither creature carries a spell in
        // creature_template.spell1-8 or creature_template_spell, and nothing in the 315100-315300
        // range is a plausible contact hit. Retail drove this from creature data the client
        // never shipped, so the magnitude stays a script constant.
        //
        // The hit is still reported as 315184 rather than dealt anonymously. An earlier version
        // called DealDamage directly, which writes health and sends nothing: the player was
        // killed by an attack that never appeared in their combat log. Going through
        // SpellNonMeleeDamage fixes that and is the more correct path anyway - the raw call
        // also bypassed absorbs, resistances and every damage-taken modifier, so a shield or a
        // defensive cooldown did nothing against it. CalculateSpellDamageTaken applies those,
        // which is what makes a near-lethal hit survivable rather than an unconditional execute.
        SpellInfo const* damageSpell = sSpellMgr->GetSpellInfo(GrandDelusions::ContainerSpell);
        if (!damageSpell)
        {
            me->DespawnOrUnsummon();
            return;
        }

        uint32 const damage = CalculatePct(player->GetMaxHealth(), _damagePctPerStrike);

        SpellNonMeleeDamage damageInfo(me, player, damageSpell->Id,
            damageSpell->GetSpellXSpellVisualId(me), SPELL_SCHOOL_MASK_SHADOW);
        me->CalculateSpellDamageTaken(&damageInfo, int32(damage), damageSpell);

        TC_LOG_DEBUG("scripts.corruption", "Thing From Beyond: struck %s for %u of %u max health "
            "(%.1f%% per strike, %u before mitigation, %u absorbed, %u resisted)",
            _summonerGuid.ToString().c_str(), damageInfo.damage, uint32(player->GetMaxHealth()),
            _damagePctPerStrike, damage, damageInfo.absorb, damageInfo.resist);

        me->SendSpellNonMeleeDamageLog(&damageInfo);
        me->DealSpellDamage(&damageInfo, false);

        // The Thing is not spent by connecting. "Pursues you for 8 sec" is the whole of what
        // the tooltip says about its life, and it says nothing about it leaving on contact;
        // the 8 seconds are the escape window, not a countdown to one guaranteed hit.
        //
        // Cascading Disaster settles it. Its tooltip reads "If you are struck by the Thing
        // From Beyond, you will be immediately afflicted by Grasping Tendrils and Eye of
        // Corruption" - being struck snares you. A snare applied by a pursuer that vanishes
        // the instant it strikes would do nothing at all, so the strike is an event during
        // the pursuit and the pursuit continues through it.
        //
        // It strikes on its own attack speed, so a player who is caught immediately eats the
        // whole pursuit and one who is caught in the last second eats a quarter of it.
        _strikeCooldown = me->GetBaseAttackTime(BASE_ATTACK);
    }

private:
    ObjectGuid _summonerGuid;
    uint32 _strikeCooldown;
    uint32 _sustainVisualTimer;
    uint32 _chaseReportTimer;
    float _damagePctPerStrike;
};

void AddSC_corruption_spell_scripts()
{
    RegisterAuraScript(spell_corruption_grasping_tendrils);
    RegisterSpellScript(spell_corruption_eye_of_corruption);
    RegisterAreaTriggerAI(at_corruption_eye_of_corruption);
    RegisterCreatureAI(npc_corruption_thing_from_beyond);
}
