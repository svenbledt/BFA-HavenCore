-- EN: Remove a real UNIT_STATE_STUNNED aura (202220) that was preloaded on Glazer's spawn
-- (npc 96680, guid 20543201) via creature_addon. A genuinely stunned caster fails every
-- spell cast with SPELL_FAILED_STUNNED (see Spell::CheckCasterAuras), so this silently
-- blocked the new npc_96680 hazard script (Pulse / Lingering Gaze, quest 39684 "Beam Me
-- Up") from ever casting anything, and made Glazer visibly stunned from the moment the
-- player entered the room instead of only after the mirror redirects the beam onto him.
-- The "imprisoned / nonattackable" look is now driven purely in C++ (root + immune-to-PC),
-- which does not block spellcasting the way a real stun aura does.
--
-- ES: Quita un aura real de UNIT_STATE_STUNNED (202220) que venia precargada en el spawn
-- de Glazer (npc 96680, guid 20543201) via creature_addon. Un caster realmente aturdido
-- falla todos los casteos con SPELL_FAILED_STUNNED (ver Spell::CheckCasterAuras), asi que
-- esto bloqueaba en silencio el nuevo script de peligros de npc_96680 (Pulse / Lingering
-- Gaze, quest 39684 "Beam Me Up"), y hacia que Glazer se viera aturdido desde que uno
-- entraba a la sala en vez de solo despues de redirigir el rayo con el espejo. El aspecto
-- de "preso / no atacable" ahora se maneja solo en C++ (root + inmune a jugadores), que no
-- bloquea el casteo de hechizos como si lo hace un aura de stun real.

UPDATE `creature_addon` SET `auras` = NULL WHERE `guid` = 20543201;
