-- EN: Wire up the previously unscripted "Glazer" NPC (96680) and "Reflective Mirror"
-- gameobject (244449) used by quest 39684 "Beam Me Up" (Demon Hunter class hall intro,
-- Vault of the Wardens). Both had an empty ScriptName, so the new C++ scripts added in
-- zone_vault_of_wardens.cpp (npc_96680 / go_244449) were never invoked by the engine.
--
-- ES: Conecta al NPC "Glazer" (96680) y al gameobject "Reflective Mirror" (244449) usados
-- por la quest 39684 "Beam Me Up" (introduccion de la orden Demon Hunter, Vault of the
-- Wardens), que no tenian ScriptName asignado. Sin esto, los nuevos scripts C++ agregados
-- en zone_vault_of_wardens.cpp (npc_96680 / go_244449) nunca eran invocados por el motor.

UPDATE `creature_template` SET `ScriptName` = 'npc_96680' WHERE `entry` = 96680;
UPDATE `gameobject_template` SET `ScriptName` = 'go_244449' WHERE `entry` = 244449;
