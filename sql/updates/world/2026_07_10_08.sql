-- EN: Fix follow-up to 2026_07_10_07.sql. That update only added FLIGHTMASTER+GOSSIP to
-- Maztha's npcflag while DROPPING her existing TRAINER (0x10) and TRAINER_PROFESSION (0x40)
-- bits, assuming they were corrupted data. That assumption was wrong: compared against
-- "Smith Argus" (creature 514, a fully working profession trainer) whose npcflag is 83
-- (TRAINER_PROFESSION|TRAINER|QUESTGIVER|GOSSIP) - nearly identical to Maztha's original 82
-- (TRAINER_PROFESSION|TRAINER|QUESTGIVER), just missing GOSSIP. Per Wowhead, Maztha (like
-- all flight masters since patch 7.3.5) no longer sells a "Flight Master's License" -
-- instead she IS the riding trainer, selling the 5 riding skill tiers directly (Apprentice
-- 4g, Journeyman 50g, Expert 250g, Artisan 5000g, Master 5000g). Restores her as a real
-- trainer (npcflag 8275 = FLIGHTMASTER|TRAINER_PROFESSION|TRAINER|QUESTGIVER|GOSSIP) with a
-- proper gossip "Train me." option (same OptionType/OptionNpcFlag pattern as every other
-- working trainer in this DB - none of the 425 existing creature_trainer rows use the
-- gossip-less MenuId=0/OptionIndex=0 "default trainer" shortcut the engine also supports, so
-- this follows the established convention instead) wired to a new trainer (Id 944919)
-- teaching the 5 riding spells, gated by SKILL_RIDING (762) rank so each tier requires the
-- previous one.
--
-- ES: Fix de seguimiento a 2026_07_10_07.sql. Esa actualizacion solo le agrego
-- FLIGHTMASTER+GOSSIP al npcflag de Maztha pero le SACO los bits TRAINER (0x10) y
-- TRAINER_PROFESSION (0x40) que ya tenia, asumiendo que eran datos corruptos. Esa suposicion
-- estaba mal: comparado con "Smith Argus" (creature 514, un entrenador de profesion
-- totalmente funcional) cuyo npcflag es 83 (TRAINER_PROFESSION|TRAINER|QUESTGIVER|GOSSIP) -
-- casi identico al 82 original de Maztha (TRAINER_PROFESSION|TRAINER|QUESTGIVER), solo le
-- faltaba GOSSIP. Segun Wowhead, Maztha (como todos los maestros de vuelo desde el parche
-- 7.3.5) ya no vende una "Flight Master's License" - en cambio ELLA ES la entrenadora de
-- monturas, vendiendo directamente los 5 niveles de la habilidad de cabalgar (Apprentice 4g,
-- Journeyman 50g, Expert 250g, Artisan 5000g, Master 5000g). La restaura como entrenadora
-- real (npcflag 8275 = FLIGHTMASTER|TRAINER_PROFESSION|TRAINER|QUESTGIVER|GOSSIP) con una
-- opcion de gossip "Train me." real (mismo patron de OptionType/OptionNpcFlag que usan todas
-- las demas entrenadoras funcionales de esta DB - ninguna de las 425 filas existentes de
-- creature_trainer usa el atajo sin gossip MenuId=0/OptionIndex=0 que tambien soporta el
-- motor, asi que se sigue la convencion establecida en vez de eso) conectada a un trainer
-- nuevo (Id 944919) que ense~na los 5 hechizos de cabalgar, con gating por rango de
-- SKILL_RIDING (762) para que cada nivel requiera el anterior.

UPDATE `creature_template` SET `npcflag` = 8275, `gossip_menu_id` = 944919 WHERE `entry` = 44919;

INSERT INTO `gossip_menu` (`MenuId`, `TextId`, `VerifiedBuild`) VALUES (944919, 0, 0);

INSERT INTO `gossip_menu_option`
(`MenuId`, `OptionIndex`, `OptionIcon`, `OptionText`, `OptionBroadcastTextId`, `OptionType`, `OptionNpcFlag`, `VerifiedBuild`)
VALUES (944919, 0, 3, 'Train me.', 3266, 5, 16, 0);

INSERT INTO `trainer` (`Id`, `Type`, `Greeting`, `VerifiedBuild`)
VALUES (944919, 2, 'I can teach you to ride, if you have the coin and the courage.', 0);

INSERT INTO `trainer_spell` (`TrainerId`, `SpellId`, `MoneyCost`, `ReqSkillLine`, `ReqSkillRank`, `ReqAbility1`, `ReqAbility2`, `ReqAbility3`, `ReqLevel`, `VerifiedBuild`) VALUES
(944919, 33388, 40000,    762, 0,   0, 0, 0, 0, 0),  -- Apprentice Riding - 4g
(944919, 33391, 500000,   762, 75,  0, 0, 0, 0, 0),  -- Journeyman Riding - 50g
(944919, 34090, 2500000,  762, 150, 0, 0, 0, 0, 0),  -- Expert Riding - 250g
(944919, 34091, 50000000, 762, 225, 0, 0, 0, 0, 0),  -- Artisan Riding - 5000g
(944919, 90265, 50000000, 762, 300, 0, 0, 0, 0, 0);  -- Master Riding - 5000g

INSERT INTO `creature_trainer` (`CreatureId`, `TrainerId`, `MenuId`, `OptionIndex`) VALUES (44919, 944919, 944919, 0);
