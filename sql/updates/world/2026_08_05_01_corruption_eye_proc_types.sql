-- The Eye spawned on any positive utility cast - learning a mount was the report.
--
-- 315169 had no `spell_proc` row, so the generated entry took PROC_SPELL_TYPE_MASK_ALL and
-- the type filter in CanSpellTriggerProcOnEvent was skipped. Mask 3 keeps damage and heals
-- and drops PROC_SPELL_TYPE_NO_DMG_HEAL, which is the bucket a mount tome shares with any
-- other cast that neither heals nor damages.
--
-- SpellPhaseMask is required here, unlike the TAKEN-only rows in 2026_08_01_01: these flags
-- are DONE, and the load path does not default it. It must stay HIT-only - Spell::finish
-- raises a second proc at PROC_SPELL_PHASE_CAST passing MASK_ALL as the event's own type
-- mask, so any wider phase would make the restriction above unenforceable.
DELETE FROM `spell_proc` WHERE `SpellId` = 315169;
INSERT INTO `spell_proc` (`SpellId`, `SpellTypeMask`, `SpellPhaseMask`) VALUES
(315169, 3, 2); -- PROC_SPELL_TYPE_DAMAGE | PROC_SPELL_TYPE_HEAL, PROC_SPELL_PHASE_HIT
