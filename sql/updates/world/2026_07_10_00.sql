-- EN: Fix quest 40254 "Forged in Fire" (Vengeance/off-spec shadow copy of quest 39683,
-- silently added to the player via zone_vault_of_wardens.cpp OnQuestAccept so the
-- other spec's artifact unlock spell is also granted on Immolanth's death) showing
-- up as a visible duplicate quest log entry. It was missing QUEST_FLAGS_TRACKING
-- (0x400), the flag that makes a quest auto-complete/reward silently without ever
-- appearing in the client quest log — same as the many other "Tracking Quest"
-- entries already in this DB.
--
-- ES: Corrige que la quest 40254 "Forged in Fire" (copia oculta de la 39683 para
-- la otra especialización, agregada en silencio por zone_vault_of_wardens.cpp
-- OnQuestAccept para otorgar también el hechizo de esa especialización al morir
-- Immolanth) apareciera como una entrada duplicada visible en el log de misiones.
-- Le faltaba el flag QUEST_FLAGS_TRACKING (0x400), que hace que una quest se
-- autocomplete y recompense en silencio sin aparecer nunca en el log del cliente
-- — igual que las demás "Tracking Quest" que ya existen en esta base de datos.

UPDATE `quest_template` SET `Flags` = `Flags` | 0x400 WHERE `ID` = 40254;
