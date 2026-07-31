-- Grand Delusions, the 40+ corruption tier.
--
-- Container 315184 procs and triggers 315186, whose only effect summons creature 161895,
-- the Thing From Beyond. Everything up to the summon is client data and already works; the
-- creature is the part with no behaviour attached to it.
--
-- AIName has to be cleared, not just joined by a ScriptName. ObjectMgr rejects a creature
-- template that carries both and keeps AIName, so leaving SmartAI in place would silently
-- discard the script - and SmartAI has no rows for this creature anyway.
UPDATE `creature_template`
SET `AIName` = '', `ScriptName` = 'npc_corruption_thing_from_beyond'
WHERE `entry` = 161895;

-- 1 RPPM on damage taken, matching the other drawback containers. The tier auras all share
-- SpellProcsPerMinuteID 86 (BaseProcRate 1.0), so only the spell type needs restricting -
-- without this row the auto-generated entry takes MASK_ALL and the player's own heals and
-- buffs summon Things as readily as being hit does.
DELETE FROM `spell_proc` WHERE `SpellId` = 315184;
INSERT INTO `spell_proc` (`SpellId`, `SpellTypeMask`) VALUES
(315184, 1); -- Grand Delusions - PROC_SPELL_TYPE_DAMAGE
