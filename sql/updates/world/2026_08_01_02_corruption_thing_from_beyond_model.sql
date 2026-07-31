-- Move the Thing from Beyond AI onto the template that has a model.
--
-- 2026_08_01_00 attached npc_corruption_thing_from_beyond to 161895, the creature the
-- extracted SpellEffect for 315186 summons. That template's only creature_template_model
-- row is display 11686 - the invisible stalker shared by 8467 templates for bunnies, kill
-- credit and conversation triggers - so the Thing spawned, chased and killed the player
-- without anything ever being drawn. Wowhead has no model for 161895 either.
--
-- 160966 is the same creature carrying display 92610 at scale 0.75. The summon itself is
-- repointed in SpellMgr::LoadSpellInfoCorrections; this moves the behaviour to match.
--
-- AIName has to be cleared, not just joined by a ScriptName: ObjectMgr keeps AIName when a
-- template carries both and silently drops the ScriptName. 160966 is marked SmartAI with no
-- smart_scripts rows of its own, which is exactly how 161895 was inert to begin with.
UPDATE `creature_template`
SET `AIName` = '', `ScriptName` = 'npc_corruption_thing_from_beyond'
WHERE `entry` = 160966;

-- Hand 161895 back to the state it was in before 2026_08_01_00 - nothing summons it now,
-- and leaving a ScriptName on it would claim otherwise.
UPDATE `creature_template`
SET `AIName` = 'SmartAI', `ScriptName` = ''
WHERE `entry` = 161895;
