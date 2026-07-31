-- EN: Fix Maztha (creature 44919, Horde flight master in Orgrimmar) not opening any
-- dialogue and not being able to teach flight paths. Her npcflag was 82
-- (QUESTGIVER|TRAINER|TRAINER_PROFESSION) - missing UNIT_NPC_FLAG_FLIGHTMASTER (0x2000,
-- what actually opens the flight map) and UNIT_NPC_FLAG_GOSSIP (0x1), and carrying
-- profession-trainer flags that don't belong on a flight master (looks like corrupted/
-- miscopied data). Compared against other working flight masters in this DB (npcflag 8193
-- or 8195) and confirmed she's also a legitimate creature_queststarter for quest 32674
-- "I Believe You Can Fly", so the correct value is 8195
-- (FLIGHTMASTER|GOSSIP|QUESTGIVER).
--
-- ES: Corrige que Maztha (creature 44919, maestra de vuelo Horda en Orgrimmar) no abriera
-- ningun dialogo ni pudiera ense~nar rutas de vuelo. Su npcflag era 82
-- (QUESTGIVER|TRAINER|TRAINER_PROFESSION) - le faltaba UNIT_NPC_FLAG_FLIGHTMASTER (0x2000,
-- lo que realmente abre el mapa de vuelo) y UNIT_NPC_FLAG_GOSSIP (0x1), y tenia flags de
-- entrenadora de profesion que no le corresponden (parece dato corrupto/mal copiado). Se
-- comparo con otras maestras de vuelo funcionales de esta DB (npcflag 8193 u 8195) y se
-- confirmo que tambien es queststarter legitima de la quest 32674 "I Believe You Can Fly",
-- asi que el valor correcto es 8195 (FLIGHTMASTER|GOSSIP|QUESTGIVER).

UPDATE `creature_template` SET `npcflag` = 8195 WHERE `entry` = 44919;
