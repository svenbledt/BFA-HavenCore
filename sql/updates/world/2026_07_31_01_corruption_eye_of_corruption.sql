-- Eye of Corruption, the 20+ corruption drawback.
--
-- The Eye already spawned before this: container 315169 procs on damage dealt and
-- triggers 315154, which creates areatrigger 22815. But 22815 had no actions in
-- `areatrigger_template_actions` and no ScriptName, and nothing anywhere cast the
-- damage spell 315161, so the Eye was purely cosmetic - it appeared, sat there for
-- its eight seconds and did nothing at all.
--
-- A template action is not usable here. AreaTrigger::DoActions fires once, from
-- HandleUnitEnterExit when a unit enters (AreaTrigger.cpp:554), while the Eye has to
-- tick every 2 seconds for as long as the player stays in range. That needs
-- AreaTriggerAI::OnPeriodicProc, which is reached only through a ScriptName.

UPDATE `areatrigger_template` SET `ScriptName` = 'at_corruption_eye_of_corruption' WHERE `Id` = 22815;

DELETE FROM `spell_script_names` WHERE `spell_id` = 315161 AND `ScriptName` = 'spell_corruption_eye_of_corruption';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(315161, 'spell_corruption_eye_of_corruption');
