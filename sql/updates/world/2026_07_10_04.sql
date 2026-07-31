-- EN: Follow-up to 2026_07_10_03.sql. That fix removed all 4 "Between Us and Freedom"
-- quest_template rows (39688/39694/40255/40256) from creature_queststarter and granted the
-- correct spec variant via a silent AddQuest() in C++. To show the native "Accept Quest"
-- popup (title/description/objectives/reward) instead of a silent grant, Creature::hasQuest()
-- needs the quest registered in creature_queststarter again (it's a static per-NPC DB lookup,
-- required both to open the native popup and for the client's "Accept" click to succeed).
-- Re-adds only the two "real" variants (39688 Havoc, 40255 Vengeance - not their 39694/40256
-- shadow copies) and sets a shared negative ExclusiveGroup on both as a safety net, so the
-- client itself treats them as mutually exclusive alternatives even if the generic
-- multi-quest gossip listing ever runs instead of the C++ spec-aware popup
-- (npc_97644::OnGossipHello, zone_vault_of_wardens.cpp).
--
-- ES: Seguimiento de 2026_07_10_03.sql. Ese fix saco las 4 filas de quest_template de
-- "Between Us and Freedom" (39688/39694/40255/40256) de creature_queststarter y otorgaba la
-- variante correcta con un AddQuest() silencioso en C++. Para mostrar el popup nativo de
-- "Aceptar Quest" (titulo/descripcion/objetivos/recompensa) en vez de un otorgamiento
-- silencioso, Creature::hasQuest() necesita que la quest este registrada de nuevo en
-- creature_queststarter (es una consulta estatica por NPC en la DB, necesaria tanto para
-- abrir el popup nativo como para que el click de "Aceptar" del cliente funcione). Se
-- vuelven a agregar solo las 2 variantes "reales" (39688 Havoc, 40255 Vengeance - no sus
-- copias sombra 39694/40256) y se les pone un ExclusiveGroup negativo compartido como red de
-- seguridad, para que el cliente mismo las trate como alternativas mutuamente excluyentes
-- aunque en algun momento corra el listado generico de gossip multi-quest en vez del popup
-- consciente de spec en C++ (npc_97644::OnGossipHello, zone_vault_of_wardens.cpp).

INSERT INTO `creature_queststarter` (`id`, `quest`) VALUES (97644, 39688), (97644, 40255);
UPDATE `quest_template_addon` SET `ExclusiveGroup` = -39688 WHERE `ID` IN (39688, 40255);
