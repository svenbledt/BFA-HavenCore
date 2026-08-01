-- Move the Thing from Beyond AI back onto 161895, the creature the summon actually names.
--
-- 2026_08_01_02 moved it to 160966 to get a model on screen: 161895's only
-- creature_template_model row is display 11686, the invisible stalker shared by 8467
-- templates for bunnies, kill credit and conversation triggers, so the Thing chased and
-- killed the player with nothing ever drawn.
--
-- That read the data backwards. Retail's Thing From Beyond is a copy of the player it is
-- chasing - a purple mirror of your own character, tethered to you - so its appearance was
-- never meant to come from the template at all, and the invisible stalker is exactly what a
-- creature that gets its model from a clone aura is supposed to carry. 161895 is also the
-- creature the encounter is built around: level 120 with HealthModifier 100, against 160966's
-- level 1 and HealthModifier 1. The clone is now cast by npc_corruption_thing_from_beyond in
-- IsSummonedBy, and the SpellMgr correction that repointed the summon is removed, so
-- SpellEffect 315186 summons 161895 as extracted.
--
-- AIName has to be cleared, not just joined by a ScriptName: ObjectMgr keeps AIName when a
-- template carries both and silently drops the ScriptName.
UPDATE `creature_template`
SET `AIName` = '', `ScriptName` = 'npc_corruption_thing_from_beyond'
WHERE `entry` = 161895;

-- Hand 160966 back to the state it was in before 2026_08_01_02 - nothing summons it now, and
-- leaving a ScriptName on it would claim otherwise.
UPDATE `creature_template`
SET `AIName` = 'SmartAI', `ScriptName` = ''
WHERE `entry` = 160966;
