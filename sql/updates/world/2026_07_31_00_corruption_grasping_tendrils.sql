-- Grasping Tendrils (corruption drawback, 1+ Corruption).
--
-- 315175 procs on damage taken and triggers the 5s snare 315176. SpellEffect.db2
-- gives 315176 BasePoints 0 / Coefficient 0 / RealPointsPerLevel 0, so the aura
-- landed at "Movement speed reduced by 0%" and had no effect on actual movement.
-- The magnitude was computed server-side in retail and never shipped in client
-- data; spell_corruption_grasping_tendrils supplies it from effective corruption.
DELETE FROM `spell_script_names` WHERE `spell_id` = 315176 AND `ScriptName` = 'spell_corruption_grasping_tendrils';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(315176, 'spell_corruption_grasping_tendrils');
