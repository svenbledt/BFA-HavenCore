# BfA 8.3 Corruption System — Design

**Date:** 2026-07-30
**Repo:** `BFA-HavenCore-Corruption` (clone of `BFA-HavenCore`, branch `feature/corruption-system`)
**Target client:** 8.3.7 build 35662

> **Repo hygiene.** This document and any other planning/AI-generated file live only in this
> clone. Before the branch is merged back into `BFA-HavenCore`, `docs/superpowers/` must be
> deleted. The main repository stays free of AI docs.

---

## 1. Problem

Corrupted gear does not work on this core. No specific repro was reported; this design is the
result of auditing the corruption code path against retail 8.3.7 behaviour.

The audit found the system is roughly half-built. The stat side exists and is correct. The
effect side is missing entirely, and the penalty side has two real bugs.

### 1.1 What already works

| Piece | Location | State |
|---|---|---|
| `CorruptionEffects.db2` loaded | `DB2Stores.cpp:111`, `:682` | Correct |
| `CorruptionEffectsEntry`, `CorruptionEffectsFlag` | `DB2Structure.h:864`, `DBCEnums.h:240` | Correct |
| `CR_CORRUPTION` (11), `CR_CORRUPTION_RESISTANCE` (12) | `Unit.h:475-476` | Correct |
| `ITEM_MOD_CORRUPTION` (22) / `_RESISTANCE` (23) → rating | `Player.cpp:8218-8222` | Correct |
| Corruption stat not scaled by item level | `Item.cpp:2240` | Correct |
| Corruption rating excluded from ilvl multiplier | `SpellInfo.cpp:622` | Correct |
| `Player::UpdateCorruption()` threshold sync | `StatSystem.cpp:735` | Present, two bugs (§4) |

### 1.2 What is missing or broken

**B1 — Bonus-granted item effects are never applied.** `ITEM_BONUS_ITEM_EFFECT_ID = 23` is
declared (`DBCEnums.h:1113`) but has no `case` in `BonusData::AddBonus`
(`Item.cpp:2738-2834`). `BonusData` (`Item.h:74-114`) has no storage for effects, and every
consumer reads `proto->Effects` — the static `ItemTemplate` list. A corrupted item therefore
contributes its Corruption *stat* but never its *effect spell*. This is the root cause.

**B2 — `PlayerConditionID` is never re-evaluated.** `UpdateCorruption()` is called only from
`UpdateRating(CR_CORRUPTION | CR_CORRUPTION_RESISTANCE)` (`Player.cpp:5527-5530`), i.e. only
when gear or rating auras change. Each `CorruptionEffects` row carries a `PlayerConditionID`,
so a player who enters an area where a condition stops matching keeps the penalty forever, and
one who leaves never regains it.

**B3 — `UpdateCorruption()` re-applies live auras.** It calls `CastSpell(this, aura, true)`
unconditionally, so every rating change re-applies penalty auras that are already present,
resetting their internal proc state.

**B4 — No corruption scripts exist.** Searching `src/` for corruption effect spell IDs and
names (Twilight Devastation, Echoing Void, Infinite Stars, Twisted Appendage, Ineffable Truth,
Void Ritual, Gushing Wound, Glimpse of Clarity, …) returns nothing. Neither the threshold
penalties nor the item effects have any mechanics behind them.

### 1.3 Related repository issues

None. All 47 open issues were reviewed and the issue search API queried; no open or closed
issue mentions corruption. The "Wrong Stats" reports (#53–#58) are MoP-era levelling items and
are unrelated.

---

## 2. Scope

**In scope**

- **Layer 1** — bonus-granted item effects reach the equip path.
- **Layer 2** — corruption state correctness and the six threshold penalties.
- **Layer 3** — the corruption effect catalogue.

**Explicit non-goals** (each needs its own spec)

- Loot rolling corruption bonuses onto drops.
- Ashjra'kamas, Shroud of Resolve: cloak ranks, corruption resistance progression, Wrathion
  quest chain, Malefic Cores.
- MOTHER purification, Corrupted Mementos, Preserved Contaminants.
- Horrific Visions and N'Zoth assaults as content.

During development and until a Layer 4 spec exists, corrupted items are produced with
`.additem <itemId> <count> <bonusListIDs>` (`cs_misc.cpp:1265`), which already accepts
semicolon-separated bonus list IDs.

### 2.1 Data authority

Every corruption number — thresholds, corruption costs, effect magnitudes, aura IDs, ranks —
comes from the untouched 8.3.7 client DB2s (`CorruptionEffects.db2`, `ItemBonus.db2`,
`ItemEffect.db2`, spell data). **No corruption values are hardcoded in C++.** Scripts implement
*mechanics only*. This is what makes the result 1:1 rather than an approximation.

The single exception is defined in §5.3 and is bounded.

---

## 3. Architecture

Four components. The two script components depend on the two core components; the dependency
never runs the other way, and the core components never name a corruption effect.

| Component | Responsibility | Depends on |
|---|---|---|
| **Bonus effect plumbing** — `Item.h`, `Item.cpp` | An item's effect set is template effects + bonus-granted effects | `sItemEffectStore` |
| **Corruption state** — `Player`, `StatSystem.cpp` | Owns effective corruption; keeps threshold auras in sync | CombatRatings, `CorruptionEffects.db2` |
| **Threshold penalty scripts** — `spell_corruption.cpp` | Mechanics for the six penalty tiers and their summons | Corruption state |
| **Corruption effect scripts** — `spell_corruption.cpp` | Mechanics for the item corruption effects | Bonus effect plumbing |

All corruption scripts live in one new file, `src/server/scripts/Spells/spell_corruption.cpp`,
registered as `AddSC_corruption_spell_scripts()` in `spell_script_loader.cpp`. Keeping the
whole corruption surface in one file makes it reviewable as a unit and keeps it out of the
class spell scripts.

---

## 4. Layer 1 — Bonus-granted item effects

Chosen approach: **TrinityCore-master parity refactor**. It fixes the declared-but-unhandled
bonus type rather than routing around it, is correct for every bonus-granted effect rather than
corruption alone, and matches upstream so future TrinityCore cherry-picks apply cleanly.

Two alternatives were considered and rejected: wiring bonus effects only into the equip and
proc paths (smaller diff, but an item's effect set would then depend on which code path asks,
and it diverges from upstream); and a standalone `CorruptionMgr` with a world-DB mapping table
(no risk to shared item code, but it hand-maintains data `ItemBonus.db2` already states
correctly and leaves B1 unfixed).

### 4.1 `BonusData` gains an effect set

In `Item.h:74`:

```cpp
ItemEffectEntry const* Effects[16];
uint32 EffectCount;
```

`BonusData::Initialize(ItemTemplate const*)` resets `EffectCount` to 0 and seeds the array from
`proto->Effects`, stopping at the array bound. `BonusData::Initialize(ItemInstance const&)`
calls the template overload first, so it is covered.

`BonusData::AddBonus` gains the missing case:

```cpp
case ITEM_BONUS_ITEM_EFFECT_ID:
    if (ItemEffectEntry const* itemEffect = sItemEffectStore.LookupEntry(values[0]))
        if (EffectCount < std::extent<decltype(Effects)>::value)
            Effects[EffectCount++] = itemEffect;
    break;
```

`sItemEffectStore` is already loaded (`DB2Stores.cpp:186`, `:756`).

### 4.2 `Item::GetEffects()`

```cpp
Trinity::IteratorPair<ItemEffectEntry const* const*> GetEffects() const;
```

Returns the range `[Effects, Effects + EffectCount)`. `Trinity::IteratorPair` already exists
(`src/common/Utilities/IteratorPair.h:32`). Callers cannot tell whether an effect came from the
template or from a bonus — that is the point.

### 4.3 Migration rule

**A call site migrates to `item->GetEffects()` if and only if it holds an `Item*`.** Sites that
only have an `ItemTemplate` keep `proto->Effects` and gain a one-line comment stating why. This
rule is what keeps the diff reviewable.

**Migrate:**

| File | Line | Context |
|---|---|---|
| `Player.cpp` | 8339 | `ApplyItemObtainSpells` |
| `Player.cpp` | 8389 | `ApplyItemEquipSpell` |
| `Player.cpp` | 8730 | `CastItemCombatSpell` |
| `Player.cpp` | 8891 | item use spells |
| `Player.cpp` | 25558 | `ApplyEquipCooldown` |
| `Item.cpp` | 470 | charge init on create |
| `Item.cpp` | 563 | charge **save** to DB — must change together with 851 (§8.1) |
| `Item.cpp` | 851-853 | charge **load** from DB — must change together with 563 (§8.1) |
| `Spell.cpp` | 4698, 6479, 6581, 6720, 6917 | charge and on-use paths |
| `SpellEffects.cpp` | 5781 | charge init on item creation |
| `SpellHistory.cpp` | 1035 | item cooldowns |
| `ItemHandler.cpp` | 1193 | — |
| `SpellHandler.cpp` | 98 | — |

**Keep `proto->Effects`** (no `Item*` available): `AuctionHouseMgr.cpp:1272`,
`BattlePayMgr.cpp:269`, `BattlePayMgr.cpp:387`, `Loot.cpp:84`, `Player.cpp:688`,
`Player.cpp:8860`, `Player.cpp:12712` (learn-spell checks), `Player.cpp:24601`
(`UpdatePotionCooldown`, keyed off `m_lastPotionId`).

### 4.4 Consequence

The stat corruptions (Masterful, Severe, Expedient, Versatile, Avoidant, Siphoner,
Strikethrough) are plain `ON_EQUIP` auras. Once `ApplyItemEquipSpell` iterates
`item->GetEffects()`, they work with no scripting at all.

---

## 5. Layer 2 — Corruption state and threshold penalties

### 5.1 Core changes

**C1 — Expose effective corruption.** Extract the expression currently trapped inside
`UpdateCorruption()` into a public accessor on `Player`:

```cpp
float GetEffectiveCorruption() const;   // GetRatingBonusValue(CR_CORRUPTION)
                                        // - GetRatingBonusValue(CR_CORRUPTION_RESISTANCE)
```

`UpdateCorruption()` uses it, and scripts scale their magnitudes from it. Without this, no
script can size a corruption-dependent effect.

**C2 — Make `UpdateCorruption()` idempotent** (fixes B3). Cast a penalty aura only when it is
absent. After the sync loop, call `Aura::RecalculateAmountOfEffects()`
(`SpellAuras.h:232`) on the corruption auras that remain applied, so magnitudes track corruption
changes without a reapply.

**C3 — Re-evaluate conditions on area change** (fixes B2). Call `UpdateCorruption()` from the
`oldArea != newArea` block at the end of `Player::UpdateArea` (`Player.cpp:7673`). C2 is a
prerequisite: without idempotency this would re-apply every penalty aura on every area
transition.

### 5.2 The six tiers

Thresholds, aura IDs and `PlayerConditionID`s are read from `CorruptionEffects.db2`. Only
mechanics are implemented.

| Tier | Retail threshold | Engine provides | To build |
|---|---|---|---|
| Grasping Tendrils | 1+ | proc aura | `spell_proc` row; AuraScript sizing the snare from `GetEffectiveCorruption()` |
| Eye of Corruption | 20+ | proc aura, summon | `spell_proc` row; CreatureAI — ramping Shadow tick every 2 s on the summoner, range-gated, 8 s lifetime |
| Grand Delusions | 40+ | proc aura, summon | `spell_proc` row; CreatureAI — fixate summoner, movement speed from corruption, 8 s despawn |
| Cascading Disaster | 60+ | nothing generic | On Thing from Beyond contact, apply Grasping Tendrils + Eye of Corruption, gated on the player holding this tier |
| Inevitable Doom | 80+ | persistent aura | AuraScript `CalcAmount` from `GetEffectiveCorruption()` for damage taken and healing received |
| Inescapable Consequences | 200+ | periodic % max-health damage | Expected data-only; small script only if the in-combat gate is not expressible in spell data |

Threshold values above are the retail reference for cross-checking the DB2 rows. The
implementation reads `CorruptionEffects.MinCorruption`; it does not hardcode these numbers.

### 5.3 Scaling sources — the one bounded exception

Retail expresses "increases with further Corruption" in data for at least some tiers
(`Curve.db2`, `SpellEffect` scaling). For each tier, implementation must first determine
whether the scaling is readable from DB2 and read it if so.

Only where it demonstrably is not readable does a formula go into C++, and then into a **single
documented constant block at the top of `spell_corruption.cpp`** — never inline at a call site.
Each entry in that block records which tier it serves and why DB2 could not supply it. This
keeps §2.1 honest and auditable.

---

## 6. Layer 3 — Corruption effect catalogue

### 6.1 Build the catalogue from DB2, not from a guide

The first implementation task is enumerating the corruption entries from `ItemBonus.db2` and
`ItemEffect.db2` on this server, producing the authoritative list of effects, ranks, corruption
costs and spell IDs.

Published guides disagree with each other and contain errors — the MMO-Champion list consulted
during research names "Avoidant" twice, once as the haste effect, which is Expedient. The
untouched 8.3.7 data is the only source that cannot be wrong. Web research is a cross-check,
not the specification.

### 6.2 Three buckets

Effects sort by how much work each needs. Bucket assignment is provisional until §6.1 completes.

**Bucket A — works once Layer 1 lands.** Plain `ON_EQUIP` stat auras: Masterful, Severe,
Expedient, Versatile, Avoidant, Siphoner, Strikethrough, three ranks each. The task is
verification, not implementation.

**Bucket B — data only, no C++.** Procs the generic engine can drive from a `spell_proc` row:
Racing Pulse, Deadly Momentum, Honed Mind, Surging Vitality, Glimpse of Clarity, Gushing Wound,
Ineffable Truth. C++ is added only for a proc condition that turns out not to be expressible in
`spell_proc`.

**Bucket C — C++ scripts.** Stateful or summoning mechanics: Echoing Void (stack build then
collapse), Infinite Stars (stacking vulnerability), Twilight Devastation (percent-of-max-health
beam), Twisted Appendage (summoned tentacle), Void Ritual (ramping secondary stats), plus any
others §6.1 surfaces.

---

## 7. Data flow

`★` marks new or changed behaviour.

```
Item created with bonus list IDs (loot / .additem / DB)
  └─ BonusData::AddBonusList → AddBonus(type, values)
       ├─ type 2  STAT, values[0] == 22 → ItemStatAllocation[i] += values[1]
       └─ type 23 ITEM_EFFECT_ID        → Effects[EffectCount++] =
                                          sItemEffectStore.LookupEntry(values[0])      ★

Equip → Player::_ApplyItemMods
  ├─ _ApplyItemBonuses → GetItemStatValue() returns raw for stat 22/23
  │                    → ApplyRatingMod(CR_CORRUPTION, val, apply)
  └─ ApplyItemEquipSpell(item) → iterates item->GetEffects()
                               → casts the corruption effect aura                      ★

ApplyRatingMod → UpdateRating(CR_CORRUPTION) → UpdateCorruption()
  └─ GetEffectiveCorruption()                                                          ★
     per CorruptionEffects row:
       Disabled                  → skip
       below MinCorruption       → RemoveAura
       PlayerCondition fails     → RemoveAura
       otherwise                 → cast only if absent                                 ★
     then RecalculateAmountOfEffects() on surviving corruption auras                   ★

Area change → Player::UpdateArea, oldArea != newArea → UpdateCorruption()               ★

Client renders the corruption meter from ActivePlayerData::CombatRatings[11] and [12]
plus its own CorruptionEffects.db2. No server packet work is required.

Unequip mirrors equip: effect aura removed, rating removed, penalties re-synced.
```

---

## 8. Error handling and edge cases

**8.1 Spell-charge serialization — introduced by this change.** Charges are written at
`Item.cpp:563` as a space-separated string sized by the effect count, and read back at
`Item.cpp:851` under the guard `tokens.size() == proto->Effects.size()`.

Two consequences, both of which must be handled:

- **Save and load must migrate together.** If only one switches to `GetEffects()`, every
  corrupted item's charge string is written at one width and read at another, so the guard
  fails and all charges are dropped on the next login.
- **The equality guard must be relaxed regardless.** An item stored before a corruption change
  — or after a future purification — legitimately has a mismatched count, and today that
  silently discards **all** its charges. Read up to `min(tokens.size(), effectCount)` instead
  of demanding exact equality.

**8.2 Effect ordering — relied upon.** `Initialize(proto)` seeds template effects first;
bonuses append after. Template effect indices, which charge slots are keyed to, therefore stay
stable. This ordering must not be changed.

**8.3 Unknown effect ID.** `sItemEffectStore.LookupEntry` returning null is skipped silently.
Bad hotfix data must not crash, and logging would fire once per item instance.

**8.4 Effect array bound.** The 16-entry cap is guarded; extras are dropped. This matches
upstream and is not reachable in practice — items carry a handful of template effects and
corruption adds one.

**8.5 Login ordering.** `_ApplyAllItemMods` runs during `Player::LoadFromDB` with a map already
assigned, so casting penalty auras there is safe. C2 idempotency prevents double application
across the login sequence.

**8.6 Negative effective corruption.** Resistance exceeding corruption yields a negative value,
which is below every `MinCorruption`, so no penalty applies. No clamping needed.

**8.7 Summoned creatures.** The Eye of Corruption and Thing from Beyond must be private to the
summoner, not attackable or lootable by others, and must despawn on timer, on summoner death,
and on zone change.

---

## 9. Verification

The repository has no test framework (`src/` contains no test target and CMake defines no
testing option), so every check is manual with a stated pass condition.

1. **Regression gate — run first.** An on-use charge trinket, a proc trinket, an heirloom (XP
   aura path) and a spec-restricted effect item all still behave. This is the blast-radius
   check for the §4.3 migration and matters more than any corruption test.
2. **Layer 1.** `.additem <id> 1 <corruptionBonusListID>` → tooltip shows Corruption N → equip
   → effect aura present and meter reads N → unequip → both gone.
3. **Layer 2 thresholds.** Cross each `CorruptionEffects.MinCorruption` boundary in turn;
   each penalty appears exactly at its threshold and lifts below it. Add a resistance source
   and confirm it lifts.
4. **B2 fix.** Cross an area boundary where a row's `PlayerConditionID` result flips; the
   penalty applies or lifts on the area change.
5. **B3 fix.** Swap gear repeatedly at a fixed corruption value; the penalty aura is never
   re-applied and its duration and stack state are undisturbed.
6. **Effects.** Per effect, proc it and confirm the combat log. For Bucket C also verify
   stacking, collapse behaviour and summon lifetimes.
7. **8.1 fix.** Equip a charged item, spend a charge, relog, confirm the remainder.

**Debug aid.** Add a `.debug corruption` GM subcommand printing effective corruption and, per
`CorruptionEffects` row, its threshold, condition result and applied state. Given the absence
of automated tests it turns most of the above from eyeballing into one readable output, at
roughly thirty lines.

---

## 10. Build order

1. §4 Layer 1 plumbing, plus the §8.1 charge fix.
2. Verification steps 1, 2 and 7 — the regression gate must pass before anything else is built
   on top.
3. §5.1 core changes C1, C2, C3, plus the §9 debug command.
4. Verification steps 3, 4 and 5.
5. §6.1 catalogue enumeration from DB2.
6. §5.2 threshold penalty mechanics.
7. §6.2 Bucket B data, then Bucket C scripts.
8. Verification step 6.

---

## 11. Merge-back checklist

Before merging `feature/corruption-system` into `BFA-HavenCore`:

1. Delete `docs/superpowers/` from the branch.
2. Confirm `git diff --stat` against `main` lists only source, SQL and script files.
3. Confirm no AI-generated planning artefacts remain anywhere in the tree.
