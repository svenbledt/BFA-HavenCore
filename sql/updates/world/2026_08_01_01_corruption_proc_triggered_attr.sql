-- Restore PROC_ATTR_TRIGGERED_CAN_PROC on the two corruption drawback containers.
--
-- 2026_07_31_02 gave 315175 and 315184 hand-written `spell_proc` rows to restrict them to
-- SpellTypeMask 1 (damage). Writing a row replaces the auto-generated entry wholesale, and
-- the generator (SpellMgr.cpp, "Generating spell proc data from SpellMap") had been setting
-- one field the rows do not: for a SPELL_AURA_PROC_TRIGGER_SPELL aura whose DBC ProcFlags
-- are TAKEN, it sets addTriggerFlag and therefore PROC_ATTR_TRIGGERED_CAN_PROC. All three
-- corruption containers are aura 42 with TAKEN flags, so 315169 still has the attribute
-- (it has no row) while 315175 and 315184 silently lost it.
--
-- Without it, SpellMgr::CanSpellTriggerProcOnEvent rejects the proc whenever the incoming
-- damage came from a spell cast with triggered = true. The client data agrees the attribute
-- belongs here: both containers carry SPELL_ATTR3_CAN_PROC_WITH_TRIGGERED (0x04000000) in
-- SpellMisc Attributes[3], which 315169 does not.
--
-- Note this is not what stops the Thing From Beyond spawning while the Eye is the only
-- thing damaging you - the Eye's damage spell 315161 carries SPELL_ATTR3_CANT_TRIGGER_PROC,
-- so it raises no proc event at all and no `spell_proc` value can change that.
DELETE FROM `spell_proc` WHERE `SpellId` IN (315175, 315184);
INSERT INTO `spell_proc` (`SpellId`, `SpellTypeMask`, `AttributesMask`) VALUES
(315175, 1, 2), -- Grasping Tendrils  - PROC_SPELL_TYPE_DAMAGE, PROC_ATTR_TRIGGERED_CAN_PROC
(315184, 1, 2); -- Grand Delusions    - PROC_SPELL_TYPE_DAMAGE, PROC_ATTR_TRIGGERED_CAN_PROC
