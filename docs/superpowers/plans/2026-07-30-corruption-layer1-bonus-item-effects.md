# Corruption Layer 1 — Bonus-Granted Item Effects Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make an item's effect set equal its template effects *plus* the effects its bonus lists grant, so that a corrupted item's effect spell actually reaches the equip path.

**Architecture:** `BonusData` gains a fixed 16-entry effect array seeded from the item template and appended to by the previously-unhandled `ITEM_BONUS_ITEM_EFFECT_ID` bonus type. `Item::GetEffects()` exposes that array as an iterator range. Every call site holding an `Item*` switches from `proto->Effects` to `item->GetEffects()`; sites holding only an `ItemTemplate*` keep `proto->Effects` and say why in a comment. Spell-charge serialization is repaired in the same pass, because widening the effect set is what makes its existing latent bugs reachable.

**Tech Stack:** C++17, CMake ≥ 3.27, Visual Studio 17 2022 (x64), MySQL 9.7, Boost 1.81, OpenSSL 3. TrinityCore-derived 8.3.7 (build 35662) server. No test framework exists in this repository — every check in this plan is a build check or a stated manual in-game check.

## Global Constraints

- **Repo hygiene.** All work happens in the clone at `F:\WorkDir\BFA-HavenCore-Corruption` on branch `feature/corruption-system`. The main repository `F:\WorkDir\BFA-HavenCore` must stay free of AI docs and planning artefacts. Never commit to the main repo. Before any eventual merge-back, `docs/superpowers/` is deleted from the branch.
- **No hardcoded corruption values.** Every corruption number comes from the untouched 8.3.7 client DB2s. This plan adds no corruption constants at all — Layer 1 is generic item-bonus plumbing that happens to unblock corruption.
- **Effect ordering is load-bearing.** `BonusData::Initialize(proto)` seeds template effects first; `AddBonus` appends after. Spell-charge slots are keyed to effect index, so template effect indices must stay stable. Do not sort, dedupe or reorder `BonusData::Effects`.
- **Charge slots are capped at 5.** `UF::ItemData::SpellCharges` is `UpdateFieldArray<int32, 5, 20, 21>` (`src/server/game/Entities/Object/Updates/UpdateFields.h:113`). `Item::GetSpellCharges` / `SetSpellCharges` index it **without bounds checking**. Every loop that pairs an effect index with a charge slot must stop at `MAX_ITEM_SPELLS`.
- **Migration rule (spec §4.3).** A call site migrates to `item->GetEffects()` **if and only if it holds an `Item*`**. Sites with only an `ItemTemplate const*` keep `proto->Effects` and gain a one-line comment stating why.
- **Unknown effect IDs are skipped silently.** `sItemEffectStore.LookupEntry` returning null must not crash and must not log — bad hotfix data would otherwise log once per item instance.
- **Commit style.** Real commit messages, imperative subject, wrapped body explaining *why*. Commit at the end of every task, never mid-task.
- **Line numbers are as of the branch tip before Task 1** (commit `1916c9d`). Every task quotes the exact code it replaces; earlier tasks shift later tasks' line numbers, so **match on the quoted code, not on the line number**. The line numbers are navigation hints only.

**Reference spec:** `docs/superpowers/specs/2026-07-30-bfa-corruption-system-design.md` (§4 Layer 1, §8.1 charge serialization, §9 verification steps 1, 2 and 7).

**Out of scope for this plan:** Layer 2 (corruption state, threshold penalties) and Layer 3 (effect catalogue) are a separate later plan. Layer 4 (loot rolling, Ashjra'kamas cloak, MOTHER purification) has no spec yet.

---

## File Structure

| File | Change | Responsibility after this plan |
|---|---|---|
| `src/server/game/Entities/Item/Item.h` | Modify | Declares `MAX_BONUS_ITEM_EFFECTS`, the `BonusData::Effects` / `EffectCount` storage, and `Item::GetEffects()` |
| `src/server/game/Entities/Item/Item.cpp` | Modify | Seeds and appends the effect array; owns charge init, save and load |
| `src/server/game/Spells/Spell.cpp` | Modify | Charge consumption, reagent accounting, enchant and recharge checks read the item's real effect set |
| `src/server/game/Spells/SpellEffects.cpp` | Modify | `EffectRechargeItem` restores charges from the item's real effect set |
| `src/server/game/Entities/Player/Player.cpp` | Modify | Obtain / equip / proc / use / equip-cooldown paths read the item's real effect set |
| `src/server/game/Handlers/SpellHandler.cpp` | Modify | In-combat usability check reads the item's real effect set |
| `src/server/game/Handlers/ItemHandler.cpp` | Modify | Critter-item check reads the item's real effect count |
| `src/server/game/AuctionHouse/AuctionHouseMgr.cpp`, `src/server/game/BattlePay/BattlePayMgr.cpp`, `src/server/game/Loot/Loot.cpp`, `src/server/game/Handlers/ToyHandler.cpp`, `src/server/game/Spells/SpellHistory.cpp` | Modify (comments only) | Documented as template-only sites |

---

### Task 0: Get the toolchain configuring and building

The rest of this plan is unverifiable without a build. CMake configure currently fails on this machine **after** MySQL is found: OpenSSL 3 is not installed. Fix that first and establish a clean baseline on unmodified sources, so any later breakage is unambiguously yours.

**Files:**
- Create: none
- Modify: none
- Test: manual — CMake configure and a full `worldserver` build

**Interfaces:**
- Consumes: nothing
- Produces: a configured build tree at `F:\WorkDir\BFA-HavenCore-Corruption\build` and a `worldserver` binary built from unmodified `feature/corruption-system`

- [ ] **Step 1: Confirm you are on the right branch in the right repository**

```bash
cd /f/WorkDir/BFA-HavenCore-Corruption
git status --short --branch
```

Expected: `## feature/corruption-system`, clean tree. If you are anywhere under `F:\WorkDir\BFA-HavenCore`, stop — that is the main repository and must not be touched.

- [ ] **Step 2: Install OpenSSL 3 (x64)**

Install "Win64 OpenSSL v3.x" (the full package, not "Light" — the headers are required) to its default location `C:\Program Files\OpenSSL-Win64`, from https://slproweb.com/products/Win32OpenSSL.html.

Then confirm the headers and import libraries exist:

```bash
ls "/c/Program Files/OpenSSL-Win64/include/openssl/ssl.h" "/c/Program Files/OpenSSL-Win64/lib/VC/x64/MD/libcrypto.lib"
```

Expected: both paths listed, no "No such file".

If you install it somewhere else, export `OPENSSL_ROOT_DIR` to that prefix before configuring.

- [ ] **Step 3: Configure**

```bash
cd /f/WorkDir/BFA-HavenCore-Corruption
rm -rf build
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DTOOLS=0
```

Expected, among the output:

```
-- Detected MySQL installations: C:/Program Files/MySQL/MySQL Server 9.7
-- Found MySQL library: C:/Program Files/MySQL/MySQL Server 9.7/lib/libmysql.lib
-- Found MySQL headers: C:/Program Files/MySQL/MySQL Server 9.7/include
-- Found Boost: C:/local/boost_1_81_0 (found suitable version "1.81.0", minimum required is "1.72")
-- Configuring done
-- Generating done
```

`BOOST_ROOT` is already set in this machine's environment to `C:\local\boost_1_81_0`; do not override it.

- [ ] **Step 4: Build the baseline**

```bash
cmake --build build --config RelWithDebInfo --target worldserver -- -m
```

Expected: build succeeds. This takes a long time on a cold tree — that is normal and is a one-off; later tasks recompile only what they touch.

- [ ] **Step 5: Record the baseline**

Note the build's warning count. You are not required to fix pre-existing warnings, but you must not add new ones in the files you touch.

No commit — this task changes no tracked file.

---

### Task 1: Give `BonusData` an effect set and handle `ITEM_BONUS_ITEM_EFFECT_ID`

This is the root-cause fix (spec §1.2 B1). `ITEM_BONUS_ITEM_EFFECT_ID = 23` is declared at `src/server/game/DataStores/DBCEnums.h:1113` but `BonusData::AddBonus` has no `case` for it, so a corrupted item contributes its Corruption *stat* and never its *effect spell*.

Nothing consumes the new storage yet. That is deliberate: this task is a pure addition and must not change any behaviour.

**Files:**
- Modify: `src/server/game/Entities/Item/Item.h:21-27` (includes), `:67` (defines), `:74-114` (`BonusData`), `:186` (accessor)
- Modify: `src/server/game/Entities/Item/Item.cpp:2672-2716` (`BonusData::Initialize`), `:2738-2834` (`BonusData::AddBonus`)
- Test: manual — build, then boot `worldserver` and confirm existing items are unaffected

**Interfaces:**
- Consumes: `sItemEffectStore` (`DB2Storage<ItemEffectEntry>`, declared `src/server/game/DataStores/DB2Stores.h:150`, already loaded at `DB2Stores.cpp:756`); `ItemEffectEntry` (`DB2Structure.h:1972`, fields `ID`, `LegacySlotIndex`, `TriggerType`, `Charges`, `CoolDownMSec`, `CategoryCoolDownMSec`, `SpellCategoryID`, `SpellID`, `ChrSpecializationID`, `ParentItemID`); `Trinity::IteratorPair` (`src/common/Utilities/IteratorPair.h:32`, provides only `begin()` and `end()` — **no `size()`**)
- Produces:
  - `#define MAX_BONUS_ITEM_EFFECTS 16`
  - `BonusData::Effects` — `ItemEffectEntry const* [MAX_BONUS_ITEM_EFFECTS]`, template effects first then bonus-granted, unused tail null
  - `BonusData::EffectCount` — `uint32`
  - `Item::GetEffects() const` → `Trinity::IteratorPair<ItemEffectEntry const* const*>`, iterating `[Effects, Effects + EffectCount)`; dereferences to `ItemEffectEntry const*`
  - `Item::GetBonus()->EffectCount` is the effect count for callers that need a count rather than a range

- [ ] **Step 1: Add the include and the bound to `Item.h`**

In `src/server/game/Entities/Item/Item.h`, add `IteratorPair.h` to the include block (keep it alphabetical, between `ItemTemplate.h` and `Loot.h`):

```cpp
#include "Object.h"
#include "Common.h"
#include "DatabaseEnvFwd.h"
#include "ItemDefines.h"
#include "ItemEnchantmentMgr.h"
#include "ItemTemplate.h"
#include "IteratorPair.h"
#include "Loot.h"
```

Then, at line 67, add the effect bound next to the existing charge bound:

```cpp
#define MAX_ITEM_SPELLS 5

// An item's effect set is its template effects plus any granted by its bonus lists
// (ITEM_BONUS_ITEM_EFFECT_ID). Sixteen matches upstream; anything past it is dropped
// rather than overflowing. Not reachable in practice - items carry a handful of
// template effects and a corruption bonus adds one.
#define MAX_BONUS_ITEM_EFFECTS 16
```

- [ ] **Step 2: Add the storage to `BonusData`**

In `src/server/game/Entities/Item/Item.h`, extend the `BonusData` public members. Add the two new members immediately after `HasFixedLevel`, before the `Initialize` declarations:

```cpp
    bool CanDisenchant;
    bool CanScrap;
    bool HasFixedLevel;
    ItemEffectEntry const* Effects[MAX_BONUS_ITEM_EFFECTS];
    uint32 EffectCount;

    void Initialize(ItemTemplate const* proto);
```

`Item::Item()` does `memset(&_bonusData, 0, sizeof(_bonusData))` (`Item.cpp:443`); `BonusData` stays trivially copyable, so that remains valid.

- [ ] **Step 3: Add `Item::GetEffects()`**

In `src/server/game/Entities/Item/Item.h`, directly below `GetBonus()` at line 186:

```cpp
    BonusData const* GetBonus() const { return &_bonusData; }

    // Template effects plus bonus-granted ones, in that order. Prefer this over
    // GetTemplate()->Effects at every site that has an Item instance in hand -
    // the template alone does not know about bonus-granted effects.
    Trinity::IteratorPair<ItemEffectEntry const* const*> GetEffects() const
    {
        return { { _bonusData.Effects, _bonusData.Effects + _bonusData.EffectCount } };
    }
```

The doubled braces are required: `IteratorPair`'s only converting constructor takes a single `std::pair`, so the inner braces build that pair.

- [ ] **Step 4: Seed the array in `BonusData::Initialize`**

In `src/server/game/Entities/Item/Item.cpp`, inside `BonusData::Initialize(ItemTemplate const* proto)`, insert between the `CanScrap` assignment and the `_state` block:

```cpp
    CanDisenchant = (proto->GetFlags() & ITEM_FLAG_NO_DISENCHANT) == 0;
    CanScrap = (proto->GetFlags4() & ITEM_FLAG4_SCRAPABLE) != 0;

    EffectCount = 0;
    for (ItemEffectEntry const* itemEffect : proto->Effects)
    {
        if (EffectCount >= MAX_BONUS_ITEM_EFFECTS)
            break;

        Effects[EffectCount++] = itemEffect;
    }

    for (uint32 i = EffectCount; i < MAX_BONUS_ITEM_EFFECTS; ++i)
        Effects[i] = nullptr;

    _state.SuffixPriority = std::numeric_limits<int32>::max();
```

`BonusData::Initialize(ItemInstance const&)` calls this overload first (`Item.cpp:2724`), so it needs no change.

- [ ] **Step 5: Handle the bonus type in `BonusData::AddBonus`**

In `src/server/game/Entities/Item/Item.cpp`, add a `case` at the end of the switch, after `ITEM_BONUS_OVERRIDE_CAN_SCRAP`:

```cpp
        case ITEM_BONUS_OVERRIDE_CAN_SCRAP:
            CanScrap = values[0] != 0;
            break;
        case ITEM_BONUS_ITEM_EFFECT_ID:
            // Skip unknown ids silently: bad hotfix data must not crash, and logging
            // here would fire once per item instance.
            if (ItemEffectEntry const* itemEffect = sItemEffectStore.LookupEntry(uint32(values[0])))
                if (EffectCount < MAX_BONUS_ITEM_EFFECTS)
                    Effects[EffectCount++] = itemEffect;
            break;
    }
}
```

`DB2Stores.h` is already included by `Item.cpp` (line 27), so `sItemEffectStore` is in scope.

- [ ] **Step 6: Build**

```bash
cd /f/WorkDir/BFA-HavenCore-Corruption
cmake --build build --config RelWithDebInfo --target worldserver -- -m
```

Expected: build succeeds with no new warnings in `Item.cpp` or `Item.h`.

- [ ] **Step 7: Boot and confirm nothing changed**

Start `worldserver`, log in, `.additem 6948 1` (Hearthstone — an on-use item), and confirm it appears and is usable. Nothing consumes `GetEffects()` yet, so any behaviour change here means the seeding loop is wrong.

Expected: identical behaviour to the Task 0 baseline.

- [ ] **Step 8: Commit**

```bash
cd /f/WorkDir/BFA-HavenCore-Corruption
git add src/server/game/Entities/Item/Item.h src/server/game/Entities/Item/Item.cpp
git commit -F - <<'EOF'
core/items: store bonus-granted item effects in BonusData

ITEM_BONUS_ITEM_EFFECT_ID (23) has been declared in DBCEnums.h without a
handler in BonusData::AddBonus, so every effect an item bonus list grants was
silently discarded. BonusData had nowhere to put one either - it tracked stats,
sockets and scaling but not effects - and every consumer read the static
ItemTemplate::Effects list instead.

BonusData now carries a sixteen-entry effect array seeded from the template and
appended to by the new case, and Item::GetEffects() exposes it as a range.
Template effects are seeded first so that existing effect indices, which spell
charge slots are keyed to, stay stable.

Nothing reads GetEffects() yet; call sites migrate in following commits.
EOF
```

---

### Task 2: Repair spell-charge serialization

Widening the effect set makes two latent bugs in the charge path reachable (spec §8.1). Fix them before any consumer migrates.

1. `Item::SaveToDB` writes one charge token per effect; `Item::LoadFromDB` reads them back under `tokens.size() == proto->Effects.size()`. If save and load disagree on width, the guard fails and **all** charges are silently dropped.
2. `Item::LoadFromDB` reads charges at line 851 but does not apply the item's bonus lists until `SetBonuses` at line 892. Reading charges before that point sees template effects only, while `SaveToDB` always runs on a fully bonused item. Switching both to `GetEffects()` is therefore *not* enough — the charge block has to move below `SetBonuses`.
3. Neither the save loop nor the load loop clamps its index to `MAX_ITEM_SPELLS`. They are safe today only because no shipped template carries more than five effects. Once `GetEffects()` can return sixteen, an unclamped loop indexes a five-element update field out of bounds.

**Files:**
- Modify: `src/server/game/Entities/Item/Item.cpp:470-472` (charge init on create), `:561-565` (charge save), `:851-854` (charge load — deleted here and re-inserted after `:892`)
- Test: manual — verification step 7 (equip a charged item, spend a charge, relog, confirm the remainder)

**Interfaces:**
- Consumes: `Item::GetEffects()`, `BonusData::EffectCount`, `MAX_ITEM_SPELLS`, `MAX_BONUS_ITEM_EFFECTS` from Task 1
- Produces: a charge string whose width is `min(EffectCount, MAX_ITEM_SPELLS)`, read back tolerantly at `min(tokens, EffectCount, MAX_ITEM_SPELLS)`

- [ ] **Step 1: Migrate charge init in `Item::Create`**

In `src/server/game/Entities/Item/Item.cpp`, replace lines 470-472:

```cpp
    for (std::size_t i = 0; i < itemProto->Effects.size(); ++i)
        if (i < 5)
            SetSpellCharges(i, itemProto->Effects[i]->Charges);
```

with:

```cpp
    uint8 chargeSlot = 0;
    for (ItemEffectEntry const* itemEffect : GetEffects())
    {
        if (chargeSlot >= MAX_ITEM_SPELLS)
            break;

        SetSpellCharges(chargeSlot++, itemEffect->Charges);
    }
```

Bonus lists are applied after `Create` returns, so `GetEffects()` yields template effects here — the same set as before. The change is for consistency and for the `MAX_ITEM_SPELLS` clamp replacing the bare `5`.

- [ ] **Step 2: Migrate the charge save loop**

In `src/server/game/Entities/Item/Item.cpp`, replace lines 561-565:

```cpp
            std::ostringstream ssSpells;
            if (ItemTemplate const* itemProto = sObjectMgr->GetItemTemplate(GetEntry()))
                for (uint8 i = 0; i < itemProto->Effects.size(); ++i)
                    ssSpells << GetSpellCharges(i) << ' ';
            stmt->setString(++index, ssSpells.str());
```

with:

```cpp
            // Charge slots are keyed by effect index and the update field only holds
            // MAX_ITEM_SPELLS of them, so write at the clamped width - LoadFromDB reads
            // back at the same one.
            std::ostringstream ssSpells;
            uint32 const chargeSlots = std::min<uint32>(_bonusData.EffectCount, MAX_ITEM_SPELLS);
            for (uint32 i = 0; i < chargeSlots; ++i)
                ssSpells << GetSpellCharges(i) << ' ';
            stmt->setString(++index, ssSpells.str());
```

The `sObjectMgr->GetItemTemplate(GetEntry())` lookup goes away: `_bonusData` is a member and already knows the effect count. `std::min` is already used in this translation unit (`Item.cpp:2652`), so no new include is needed.

- [ ] **Step 3: Delete the charge load block from its current position**

In `src/server/game/Entities/Item/Item.cpp`, delete lines 851-854 entirely:

```cpp
    Tokenizer tokens(fields[6].GetString(), ' ', proto->Effects.size());
    if (tokens.size() == proto->Effects.size())
        for (uint8 i = 0; i < proto->Effects.size(); ++i)
            SetSpellCharges(i, atoi(tokens[i]));

```

leaving the surrounding code as:

```cpp
    uint32 duration = fields[5].GetUInt32();
    SetExpiration(duration);
    // update duration if need, and remove if not need
    if ((proto->GetDuration() == 0) != (duration == 0))
    {
        SetExpiration(proto->GetDuration());
        need_save = true;
    }

    SetItemFlags(ItemFieldFlags(itemFlags));
```

- [ ] **Step 4: Re-insert the charge load block below `SetBonuses`**

In `src/server/game/Entities/Item/Item.cpp`, immediately after the `SetBonuses` call (line 892 before the deletion in Step 3):

```cpp
    Tokenizer bonusListString(fields[18].GetString(), ' ');
    std::vector<int32> bonusListIDs;
    bonusListIDs.reserve(bonusListString.size());
    for (char const* token : bonusListString)
        bonusListIDs.push_back(atoi(token));
    SetBonuses(std::move(bonusListIDs));

    // Charges are keyed by effect index and bonus lists can add effects, so this has to
    // run after SetBonuses - before it, only the template effects are known. An item
    // stored before its bonus list changed legitimately has fewer tokens than effects;
    // read what is there rather than discarding every charge on a count mismatch.
    Tokenizer tokens(fields[6].GetString(), ' ', _bonusData.EffectCount);
    uint32 const chargeSlots = std::min({ uint32(tokens.size()), _bonusData.EffectCount, uint32(MAX_ITEM_SPELLS) });
    for (uint32 i = 0; i < chargeSlots; ++i)
        SetSpellCharges(i, atoi(tokens[i]));

    SetModifier(ITEM_MODIFIER_TRANSMOG_APPEARANCE_ALL_SPECS, fields[19].GetUInt32());
```

Nothing between the block's old and new positions reads or writes spell charges (`SetItemFlags`, durability, played time, text, battle-pet modifiers, context, bonus lists), so the move is behaviour-preserving for uncorrupted items.

- [ ] **Step 5: Build**

```bash
cd /f/WorkDir/BFA-HavenCore-Corruption
cmake --build build --config RelWithDebInfo --target worldserver -- -m
```

Expected: build succeeds with no new warnings in `Item.cpp`.

- [ ] **Step 6: Verify the charge round-trip (spec §9 step 7)**

Start `worldserver`, then in-game:

1. `.additem 5512 5` — Healthstone, an item with charges.
2. Use one.
3. `.save`, log out, log back in.

Expected: the remaining charge count is what it was before the relog. Before this task an equality-guard failure would have reset it.

Also confirm the stored string directly:

```sql
SELECT guid, itemEntry, charges FROM characters.item_instance WHERE itemEntry = 5512;
```

Expected: a space-separated list of integers, one per effect slot up to five, matching what the client shows.

- [ ] **Step 7: Commit**

```bash
cd /f/WorkDir/BFA-HavenCore-Corruption
git add src/server/game/Entities/Item/Item.cpp
git commit -F - <<'EOF'
core/items: fix spell charge serialization for bonus-granted effects

Three problems, all made reachable by BonusData now tracking bonus-granted
effects:

LoadFromDB parsed charges at the top of the function but did not apply the
item's bonus lists until SetBonuses, ~40 lines later. It therefore saw template
effects only, while SaveToDB always runs on a fully bonused item - the two would
disagree on the string's width. The charge block moves below SetBonuses; nothing
in between touches charges.

The load guard demanded tokens.size() == effect count and dropped every charge
on a mismatch. An item stored before its bonus list changed legitimately
mismatches, so read min(tokens, effects) instead. This is also what carries
existing item_instance rows across the change: they were written at template
width and are now read back at template width.

Neither the save nor the load loop clamped its index, but SpellCharges is a
five-element update field indexed without bounds checking. Both now stop at
MAX_ITEM_SPELLS, as the other charge loops in the tree already did.
EOF
```

---

### Task 3: Migrate the charge-consuming call sites

Six sites pair an effect index with a charge slot. All of them hold an `Item*`, so all migrate.

**Files:**
- Modify: `src/server/game/Spells/Spell.cpp:4698` (`TakeCastItem`), `:4903` (`TakeReagents`), `:6479` (`CheckCast` — charges remaining), `:6581` (`CheckCast` — reagent accounting), `:6917` (`CheckCast` — `SPELL_EFFECT_RECHARGE_ITEM`)
- Modify: `src/server/game/Spells/SpellEffects.cpp:5778-5784` (`EffectRechargeItem`)
- Test: manual — verification step 1 (charge trinket regression)

**Interfaces:**
- Consumes: `Item::GetEffects()` and `MAX_ITEM_SPELLS` from Task 1. Both files already `#include "Item.h"`.
- Produces: nothing new

Note: `Spell` has its own unrelated `GetEffects()` returning spell effects (used at `Spell.cpp:6489`). `item->GetEffects()` and the bare `GetEffects()` are different functions — do not confuse them, and never drop the `item->` / `m_CastItem->` qualifier.

- [ ] **Step 1: `Spell.cpp:4698` — charge consumption**

Replace:

```cpp
    for (uint8 i = 0; i < proto->Effects.size() && i < 5; ++i)
    {
        // item has limited charges
        if (proto->Effects[i]->Charges)
        {
            if (proto->Effects[i]->Charges < 0)
                expendable = true;

            int32 charges = m_CastItem->GetSpellCharges(i);
```

with:

```cpp
    uint8 i = 0;
    for (ItemEffectEntry const* itemEffect : m_CastItem->GetEffects())
    {
        if (i >= MAX_ITEM_SPELLS)
            break;

        // item has limited charges
        if (itemEffect->Charges)
        {
            if (itemEffect->Charges < 0)
                expendable = true;

            int32 charges = m_CastItem->GetSpellCharges(i);
```

and close the loop by incrementing `i` at the end of the body:

```cpp
            // all charges used
            withoutCharges = (charges == 0);
        }

        ++i;
    }
```

Leave the `proto` local alone — it is still used for `proto->GetMaxStackSize()` inside the loop and below it.

- [ ] **Step 2: `Spell.cpp:4903` — reagent accounting in `TakeReagents`**

Replace:

```cpp
            for (uint8 s = 0; s < castItemTemplate->Effects.size() && s < 5; ++s)
            {
                // CastItem will be used up and does not count as reagent
                int32 charges = m_CastItem->GetSpellCharges(s);
                if (castItemTemplate->Effects[s]->Charges < 0 && abs(charges) < 2)
                {
                    ++itemcount;
                    break;
                }
            }
```

with:

```cpp
            uint8 s = 0;
            for (ItemEffectEntry const* itemEffect : m_CastItem->GetEffects())
            {
                if (s >= MAX_ITEM_SPELLS)
                    break;

                // CastItem will be used up and does not count as reagent
                int32 charges = m_CastItem->GetSpellCharges(s++);
                if (itemEffect->Charges < 0 && abs(charges) < 2)
                {
                    ++itemcount;
                    break;
                }
            }
```

`castItemTemplate` is `m_CastItem ? m_CastItem->GetTemplate() : nullptr` (`Spell.cpp:4882`) and this block is inside `if (castItemTemplate && ...)`, so `m_CastItem` is non-null here. Leave `castItemTemplate` in place — the enclosing condition still uses it.

- [ ] **Step 3: `Spell.cpp:6479` — charges-remaining check**

Replace:

```cpp
        for (uint8 i = 0; i < proto->Effects.size() && i < 5; ++i)
            if (proto->Effects[i]->Charges)
                if (m_CastItem->GetSpellCharges(i) == 0)
                    return SPELL_FAILED_NO_CHARGES_REMAIN;
```

with:

```cpp
        uint8 chargeSlot = 0;
        for (ItemEffectEntry const* itemEffect : m_CastItem->GetEffects())
        {
            if (chargeSlot >= MAX_ITEM_SPELLS)
                break;

            if (itemEffect->Charges && m_CastItem->GetSpellCharges(chargeSlot) == 0)
                return SPELL_FAILED_NO_CHARGES_REMAIN;

            ++chargeSlot;
        }
```

Leave the `proto` local — it is still used for the consumable check immediately below.

- [ ] **Step 4: `Spell.cpp:6581` — reagent accounting in `CheckCast`**

Replace:

```cpp
                    for (uint8 s = 0; s < proto->Effects.size() && s < 5; ++s)
                    {
                        // CastItem will be used up and does not count as reagent
                        int32 charges = m_CastItem->GetSpellCharges(s);
                        if (proto->Effects[s]->Charges < 0 && abs(charges) < 2)
                        {
                            ++itemcount;
                            break;
                        }
                    }
```

with:

```cpp
                    uint8 s = 0;
                    for (ItemEffectEntry const* itemEffect : m_CastItem->GetEffects())
                    {
                        if (s >= MAX_ITEM_SPELLS)
                            break;

                        // CastItem will be used up and does not count as reagent
                        int32 charges = m_CastItem->GetSpellCharges(s++);
                        if (itemEffect->Charges < 0 && abs(charges) < 2)
                        {
                            ++itemcount;
                            break;
                        }
                    }
```

Leave the `proto` local and its null-guard — the guard returns `SPELL_FAILED_ITEM_NOT_READY` and is a real check.

- [ ] **Step 5: `Spell.cpp:6917` — `SPELL_EFFECT_RECHARGE_ITEM` check**

Replace:

```cpp
                 if (Item* item = player->GetItemByEntry(itemId))
                 {
                     for (uint8 x = 0; x < proto->Effects.size() && x < 5; ++x)
                         if (proto->Effects[x]->Charges != 0 && item->GetSpellCharges(x) == proto->Effects[x]->Charges)
                             return SPELL_FAILED_ITEM_AT_MAX_CHARGES;
                 }
```

with:

```cpp
                 if (Item* item = player->GetItemByEntry(itemId))
                 {
                     uint8 x = 0;
                     for (ItemEffectEntry const* itemEffect : item->GetEffects())
                     {
                         if (x >= MAX_ITEM_SPELLS)
                             break;

                         if (itemEffect->Charges != 0 && item->GetSpellCharges(x) == itemEffect->Charges)
                             return SPELL_FAILED_ITEM_AT_MAX_CHARGES;

                         ++x;
                     }
                 }
```

Leave the `proto` lookup above it — its null-guard returns `SPELL_FAILED_ITEM_AT_MAX_CHARGES` for unknown item ids.

- [ ] **Step 6: `SpellEffects.cpp:5778` — `EffectRechargeItem`**

Replace:

```cpp
    if (Item* item = player->GetItemByEntry(effectInfo->ItemType))
    {
        ItemTemplate const* proto = item->GetTemplate();
        for (size_t x = 0; x < proto->Effects.size() && x < 5; ++x)
            item->SetSpellCharges(x, proto->Effects[x]->Charges);
        item->SetState(ITEM_CHANGED, player);
    }
```

with:

```cpp
    if (Item* item = player->GetItemByEntry(effectInfo->ItemType))
    {
        uint8 x = 0;
        for (ItemEffectEntry const* itemEffect : item->GetEffects())
        {
            if (x >= MAX_ITEM_SPELLS)
                break;

            item->SetSpellCharges(x++, itemEffect->Charges);
        }

        item->SetState(ITEM_CHANGED, player);
    }
```

The `proto` local becomes unused and is removed.

- [ ] **Step 7: Build**

```bash
cd /f/WorkDir/BFA-HavenCore-Corruption
cmake --build build --config RelWithDebInfo --target worldserver -- -m
```

Expected: build succeeds. In particular there must be no "unused variable 'proto'" warning in `SpellEffects.cpp` — if there is, you left the local behind.

- [ ] **Step 8: Verify charge behaviour (spec §9 step 1, charge half)**

Start `worldserver`, then in-game:

1. `.additem 5512 3` — Healthstone. Use one. Expected: charge count drops by one and the stack behaves as before.
2. `.additem 6948 1` — Hearthstone. Use it. Expected: casts, no charge consumed, item stays.
3. Use a charged item down to zero. Expected: the item is consumed, and attempting a further cast reports "no charges remain" rather than succeeding.

- [ ] **Step 9: Commit**

```bash
cd /f/WorkDir/BFA-HavenCore-Corruption
git add src/server/game/Spells/Spell.cpp src/server/game/Spells/SpellEffects.cpp
git commit -F - <<'EOF'
core/spells: read charges from the item's effect set, not its template

Charge slots are keyed by effect index, so every site that pairs the two has to
agree on what "the item's effects" means. These six read ItemTemplate::Effects
while Item::SaveToDB now serializes at the bonused width; a bonus-granted
effect would have shifted the two out of step.

The hardcoded "&& i < 5" bounds become MAX_ITEM_SPELLS, which is what they were
always standing in for.
EOF
```

---

### Task 4: Migrate the effect-iterating call sites

Eight sites iterate an item's effects without touching charges. All hold an `Item*`.

**Files:**
- Modify: `src/server/game/Entities/Player/Player.cpp:8336` (`ApplyItemObtainSpells`), `:8380` (`ApplyItemEquipSpell`), `:8723` (`CastItemCombatSpell`), `:8891` (`CastItemUseSpell` — on-use loop), `:25551` (`ApplyEquipCooldown`)
- Modify: `src/server/game/Spells/Spell.cpp:6719-6727` (enchant-target usability)
- Modify: `src/server/game/Handlers/ItemHandler.cpp:1193` (`HandleUseCritterItem`)
- Modify: `src/server/game/Handlers/SpellHandler.cpp:98` (in-combat usability)
- Test: manual — verification steps 1 and 2

**Interfaces:**
- Consumes: `Item::GetEffects()` and `Item::GetBonus()->EffectCount` from Task 1
- Produces: nothing new. `ApplyItemEquipSpell` is the site that makes corruption work — every stat corruption (Masterful, Severe, Expedient, Versatile, Avoidant, Siphoner, Strikethrough) is a plain `ON_EQUIP` aura and needs no script once this lands.

- [ ] **Step 1: `Player.cpp:8336` — `ApplyItemObtainSpells`**

Replace the whole function body:

```cpp
void Player::ApplyItemObtainSpells(Item* item, bool apply)
{
    ItemTemplate const* itemTemplate = item->GetTemplate();
    for (uint8 i = 0; i < itemTemplate->Effects.size(); ++i)
    {
        if (itemTemplate->Effects[i]->TriggerType != ITEM_SPELLTRIGGER_ON_OBTAIN) // On obtain trigger
            continue;

        int32 const spellId = itemTemplate->Effects[i]->SpellID;
        if (spellId <= 0)
            continue;
```

with:

```cpp
void Player::ApplyItemObtainSpells(Item* item, bool apply)
{
    for (ItemEffectEntry const* effectData : item->GetEffects())
    {
        if (effectData->TriggerType != ITEM_SPELLTRIGGER_ON_OBTAIN) // On obtain trigger
            continue;

        int32 const spellId = effectData->SpellID;
        if (spellId <= 0)
            continue;
```

The rest of the body is unchanged. The `itemTemplate` local becomes unused and is removed.

- [ ] **Step 2: `Player.cpp:8380` — `ApplyItemEquipSpell`**

Replace:

```cpp
    ItemTemplate const* proto = item->GetTemplate();
    if (!proto)
        return;

    for (uint8 i = 0; i < proto->Effects.size(); ++i)
    {
        ItemEffectEntry const* effectData = proto->Effects[i];

        // wrong triggering type
```

with:

```cpp
    if (!item->GetTemplate())
        return;

    for (ItemEffectEntry const* effectData : item->GetEffects())
    {
        // wrong triggering type
```

The rest of the loop body is unchanged. The null-guard is kept — it is a real sanity check on the item's template — but the local is dropped since nothing else in the function uses it.

- [ ] **Step 3: `Player.cpp:8723` — `CastItemCombatSpell`**

Replace:

```cpp
        for (uint8 i = 0; i < proto->Effects.size(); ++i)
        {
            ItemEffectEntry const* effectData = proto->Effects[i];

            // wrong triggering type
            if (effectData->TriggerType != ITEM_SPELLTRIGGER_CHANCE_ON_HIT)
                continue;
```

with:

```cpp
        for (ItemEffectEntry const* effectData : item->GetEffects())
        {
            // wrong triggering type
            if (effectData->TriggerType != ITEM_SPELLTRIGGER_CHANCE_ON_HIT)
                continue;
```

Keep the `proto` parameter — it is still used for `proto->SpellPPMRate` inside the loop.

- [ ] **Step 4: `Player.cpp:8891` — `CastItemUseSpell` on-use loop**

Replace:

```cpp
    // item spells cast at use
    for (uint8 i = 0; i < proto->Effects.size(); ++i)
    {
        ItemEffectEntry const* effectData = proto->Effects[i];

        // wrong triggering type
        if (effectData->TriggerType != ITEM_SPELLTRIGGER_ON_USE)
            continue;
```

with:

```cpp
    // item spells cast at use
    for (ItemEffectEntry const* effectData : item->GetEffects())
    {
        // wrong triggering type
        if (effectData->TriggerType != ITEM_SPELLTRIGGER_ON_USE)
            continue;
```

Keep the `proto` local — the special learning case above (`Player.cpp:8860`) and the error log inside the loop (`proto->GetId()`) still use it.

- [ ] **Step 5: `Player.cpp:25551` — `ApplyEquipCooldown`**

Replace:

```cpp
    std::chrono::steady_clock::time_point now = GameTime::GetGameTimeSteadyPoint();
    for (uint8 i = 0; i < proto->Effects.size(); ++i)
    {
        ItemEffectEntry const* effectData = proto->Effects[i];

        // apply proc cooldown to equip auras if we have any
```

with:

```cpp
    std::chrono::steady_clock::time_point now = GameTime::GetGameTimeSteadyPoint();
    for (ItemEffectEntry const* effectData : pItem->GetEffects())
    {
        // apply proc cooldown to equip auras if we have any
```

Keep the `proto` local — the `ITEM_FLAG_NO_EQUIP_COOLDOWN` guard above still uses it.

- [ ] **Step 6: `Spell.cpp:6719` — enchant-target usability**

Replace:

```cpp
                bool isItemUsable = false;
                ItemTemplate const* proto = targetItem->GetTemplate();
                for (uint8 e = 0; e < proto->Effects.size(); ++e)
                {
                    if (proto->Effects[e]->SpellID && proto->Effects[e]->TriggerType == ITEM_SPELLTRIGGER_ON_USE)
                    {
                        isItemUsable = true;
                        break;
                    }
                }
```

with:

```cpp
                bool isItemUsable = false;
                for (ItemEffectEntry const* itemEffect : targetItem->GetEffects())
                {
                    if (itemEffect->SpellID && itemEffect->TriggerType == ITEM_SPELLTRIGGER_ON_USE)
                    {
                        isItemUsable = true;
                        break;
                    }
                }
```

The `proto` local becomes unused and is removed.

- [ ] **Step 7: `ItemHandler.cpp:1193` — `HandleUseCritterItem`**

Replace:

```cpp
    if (item->GetTemplate()->Effects.size() < 2)
        return;
```

with:

```cpp
    if (item->GetBonus()->EffectCount < 2)
        return;
```

`Trinity::IteratorPair` exposes only `begin()` and `end()`, so a count comes from `GetBonus()->EffectCount` rather than from `GetEffects()`.

- [ ] **Step 8: `SpellHandler.cpp:98` — in-combat usability**

Replace:

```cpp
        for (uint32 i = 0; i < proto->Effects.size(); ++i)
        {
            if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(proto->Effects[i]->SpellID))
```

with:

```cpp
        for (ItemEffectEntry const* itemEffect : item->GetEffects())
        {
            if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itemEffect->SpellID))
```

The rest of the block is unchanged. Keep the `proto` local — it is used throughout the rest of the handler.

- [ ] **Step 9: Build**

```bash
cd /f/WorkDir/BFA-HavenCore-Corruption
cmake --build build --config RelWithDebInfo --target worldserver -- -m
```

Expected: build succeeds with no unused-variable warnings in `Player.cpp` or `Spell.cpp`.

- [ ] **Step 10: Regression gate (spec §9 step 1)**

This is the blast-radius check for the whole migration and matters more than any corruption test. Start `worldserver` and confirm each of the following still behaves:

1. **On-use charge trinket.** `.additem 5512 3` (Healthstone), use one. Expected: heals, charge count drops.
2. **Proc trinket.** Equip any weapon with a `CHANCE_ON_HIT` effect and attack a target dummy until it procs. Expected: the proc appears in the combat log as before.
3. **Heirloom XP aura.** Pick any heirloom — `SELECT ItemID FROM hotfixes.heirloom LIMIT 5;` — and `.additem <ItemID> 1`. Equip it on a character below the heirloom's level cap. Expected: the XP bonus aura is present. Equip it on a character above the cap. Expected: no XP aura. This is the `CanApplyHeirloomXpBonus` branch in `ApplyItemEquipSpell`, which the migration runs straight through.
4. **Spec-restricted effect.** Equip an item whose effect has a non-zero `ChrSpecializationID` while in a non-matching spec. Expected: the effect is not applied. Switch to the matching spec. Expected: it is.
5. **Equip cooldown.** Equip an on-use trinket. Expected: a 30-second cooldown is shown on its use effect.
6. **Toy.** Use a learned toy from the collections UI. Expected: it casts (this path is `ToyHandler.cpp`, deliberately unmigrated — confirm it did not regress).

Any failure here means a migrated site changed behaviour; fix it before continuing.

- [ ] **Step 11: Commit**

```bash
cd /f/WorkDir/BFA-HavenCore-Corruption
git add src/server/game/Entities/Player/Player.cpp src/server/game/Spells/Spell.cpp src/server/game/Handlers/ItemHandler.cpp src/server/game/Handlers/SpellHandler.cpp
git commit -F - <<'EOF'
core: apply item effects granted by bonus lists

Every path that acts on an item's effects read ItemTemplate::Effects, which
knows nothing about the effects a bonus list grants. A corrupted Battle for
Azeroth item therefore contributed its Corruption stat, because that arrives as
a plain stat bonus, but never its effect spell.

The eight sites here all hold an Item*, so they move to Item::GetEffects().
ApplyItemEquipSpell is the one that matters most: the stat corruptions
(Masterful, Severe, Expedient, Versatile, Avoidant, Siphoner, Strikethrough)
are ordinary ON_EQUIP auras and start working with no scripting at all.

Sites that only ever see an ItemTemplate keep reading it; they are annotated
separately.
EOF
```

---

### Task 5: Annotate the sites that deliberately keep `proto->Effects`

Ten sites do not hold an `Item*` and must keep reading the template. Without a note, the next reader cannot tell them apart from sites that were simply missed.

**Files:**
- Modify: `src/server/game/AuctionHouse/AuctionHouseMgr.cpp:1272`, `src/server/game/BattlePay/BattlePayMgr.cpp:269` and `:387`, `src/server/game/Loot/Loot.cpp:84`, `src/server/game/Entities/Player/Player.cpp:688`, `:8860`, `:12712`, `:24601`, `src/server/game/Handlers/ToyHandler.cpp:64`, `src/server/game/Spells/SpellHistory.cpp:1035`
- Test: build only — these are comment-only changes

**Interfaces:**
- Consumes: nothing
- Produces: nothing

- [ ] **Step 1: Annotate the template-only sites**

Add a comment immediately above each listed statement. Use the wording below verbatim so the set is greppable.

`AuctionHouseMgr.cpp:1272`, above `if (itemTemplate->Effects.size() >= 2 && ...)`:

```cpp
                // Template effects only: an auction listing is described by item entry and
                // bonus list ids, with no Item instance to ask for bonus-granted effects.
```

`BattlePayMgr.cpp:269`, above `for (auto itr : itemTemplate->Effects)`, and `:387`, above `for (auto effectData : itemTemplate->Effects)`:

```cpp
        // Template effects only: the shop entry names an item id, not an instance.
```

`Loot.cpp:84`, above the `ITEM_FLAG_HIDE_UNUSABLE_RECIPE` condition:

```cpp
        // Template effects only: loot is filtered before any Item is created.
```

`Player.cpp:688`, above `if (iProto->Effects.size() >= 1)`:

```cpp
                    // Template effects only: this walks stored item ids, not instances.
```

`Player.cpp:8860`, above `if (proto->Effects.size() >= 2)`:

```cpp
    // Template effects only: the learn-spell special case is keyed to the two fixed
    // template effect slots, which bonus-granted effects are appended after.
```

`Player.cpp:12712`, above `if (proto->Effects.size() >= 2)`:

```cpp
    // Template effects only: same fixed learn-spell slots as CastItemUseSpell.
```

`Player.cpp:24601`, above `for (uint8 idx = 0; idx < proto->Effects.size(); ++idx)`:

```cpp
            // Template effects only: UpdatePotionCooldown is keyed off m_lastPotionId,
            // an item id kept after the Item itself is gone.
```

`ToyHandler.cpp:64`, above the `std::find_if`:

```cpp
    // Template effects only: a toy is used from the collection by item entry - the
    // player need not own an instance of it.
```

`SpellHistory.cpp:1035`, above `for (ItemEffectEntry const* itemEffect : proto->Effects)`:

```cpp
            // Template effects only: this function receives an item id, not an Item*, so a
            // bonus-granted effect's cooldown override is out of reach here. That is a gap
            // for any future bonus-granted on-use effect; corruption effects are passive.
```

- [ ] **Step 2: Build**

```bash
cd /f/WorkDir/BFA-HavenCore-Corruption
cmake --build build --config RelWithDebInfo --target worldserver -- -m
```

Expected: build succeeds.

- [ ] **Step 3: Confirm nothing was missed**

```bash
cd /f/WorkDir/BFA-HavenCore-Corruption
grep -rln -- "->Effects\b" src/server/game --include=*.cpp
```

Expected: exactly these seven files and no others —

```
src/server/game/AuctionHouse/AuctionHouseMgr.cpp
src/server/game/BattlePay/BattlePayMgr.cpp
src/server/game/Entities/Item/Item.cpp
src/server/game/Entities/Player/Player.cpp
src/server/game/Handlers/ToyHandler.cpp
src/server/game/Loot/Loot.cpp
src/server/game/Spells/SpellHistory.cpp
```

`Item.cpp` is on the list because `BonusData::Initialize` is where template effects are *read into* the array — that one is the source of the whole mechanism, not a missed site.

Then re-run without `-l` and read the hits. Several of the ten sites match on more than one line (`AuctionHouseMgr.cpp` on three, the learn-spell checks on four), so count sites, not lines: every hit must sit directly under one of the annotations from Step 1, or be the `Initialize` seeding loop. Any other hit is a missed migration.

- [ ] **Step 4: Commit**

```bash
cd /f/WorkDir/BFA-HavenCore-Corruption
git add src/server/game/AuctionHouse/AuctionHouseMgr.cpp src/server/game/BattlePay/BattlePayMgr.cpp src/server/game/Loot/Loot.cpp src/server/game/Entities/Player/Player.cpp src/server/game/Handlers/ToyHandler.cpp src/server/game/Spells/SpellHistory.cpp
git commit -F - <<'EOF'
core: note why the remaining sites still read ItemTemplate::Effects

These ten have only an item id or an ItemTemplate in hand, so they cannot ask
an instance for its bonus-granted effects. Saying so at each site keeps them
distinguishable from sites that were simply missed.

SpellHistory::GetCooldownDurations is the one with real consequences: a
bonus-granted on-use effect cannot override its cooldown there. Fixing it means
threading an Item* through the cooldown API, which is its own change.
EOF
```

---

### Task 6: Verify Layer 1 end to end

The regression gate (step 1) ran in Task 4 and the charge round-trip (step 7) ran in Task 2. This task is spec §9 step 2 — proving a corrupted item actually works — plus a final re-run of both earlier gates on the finished branch.

**Files:**
- Create: none
- Modify: none
- Test: manual, in-game

**Interfaces:**
- Consumes: everything from Tasks 1-5
- Produces: a stated pass/fail for spec §9 steps 1, 2 and 7

- [ ] **Step 1: Find a real corruption bonus list on this server**

Corruption arrives as a bonus list carrying both a stat bonus (`Type = 2`, `Value1 = 22` = `ITEM_MOD_CORRUPTION`) and an effect bonus (`Type = 23` = `ITEM_BONUS_ITEM_EFFECT_ID`). Query the hotfix tables for lists that carry both:

```sql
SELECT s.ParentItemBonusListID AS BonusListID,
       s.Value2                AS CorruptionAmount,
       e.Value1                AS ItemEffectID,
       ie.SpellID              AS EffectSpellID
FROM hotfixes.item_bonus s
JOIN hotfixes.item_bonus e
  ON e.ParentItemBonusListID = s.ParentItemBonusListID
 AND e.Type = 23
JOIN hotfixes.item_effect ie
  ON ie.ID = e.Value1
WHERE s.Type = 2 AND s.Value1 = 22
ORDER BY s.Value2;
```

Expected: a non-empty result. Pick one row and note its `BonusListID`, `CorruptionAmount` and `EffectSpellID`.

If the result is empty, stop and report it: it means `ItemBonus.db2` / `ItemEffect.db2` were not extracted from the 8.3.7 client into the hotfix database, and no amount of C++ will make corruption work. That is a data problem, not a code problem.

- [ ] **Step 2: Turn on the log line that proves the equip path ran**

Before starting `worldserver`, add this to `worldserver.conf` (level 2 is `LOG_LEVEL_DEBUG`; `src/common/Logging/LogCommon.h:25`):

```
Logger.entities.player=2,Console Server
```

`Player::ApplyEquipSpell` logs every equip spell it casts (`Player.cpp:8430`):

```
Player::ApplyEquipSpell: Player '<name>' (<guid>) cast item equip spell (ID: <spellId>)
```

That line is the direct evidence that the migrated loop reached the bonus-granted effect. Without it you are inferring from the buff frame.

- [ ] **Step 3: Create a corrupted item (spec §9 step 2)**

Pick any item you can equip — shift-click one from your own bags to get its id, or take one from the item link in the tooltip. Then, with `<BonusListID>` from Step 1:

```
.additem <equippableItemId> 1 <BonusListID>
```

Expected: the item appears and its tooltip shows `Corruption <CorruptionAmount>`.

- [ ] **Step 4: Equip it and confirm the effect aura**

Equip the item.

Expected, all four:

1. `worldserver` logs `Player::ApplyEquipSpell: ... cast item equip spell (ID: <EffectSpellID>)` with the `EffectSpellID` from Step 1. **This is the whole point of the plan** — before Task 1 this line never appeared, because the effect was never in the item's effect set.
2. The effect aura is visible on the character's buff frame.
3. The client's corruption meter reads `CorruptionAmount`.
4. For a stat corruption (Masterful, Severe, Expedient, Versatile, Avoidant, Siphoner, Strikethrough) the matching secondary stat rises on the character sheet.

`worldserver` must log no errors about unknown spells or item effects.

- [ ] **Step 5: Unequip and confirm both come off**

Unequip the item.

Expected: the effect aura is removed and the corruption meter returns to zero.

- [ ] **Step 6: Re-run the earlier gates on the finished branch**

Re-run Task 4 Step 10 (all six regression checks) and Task 2 Step 6 (charge round-trip) against the completed branch. Both must still pass — later tasks touched the same files.

- [ ] **Step 7: Confirm the main repository is untouched**

```bash
cd /f/WorkDir/BFA-HavenCore
git status --short --branch
git log --oneline -1
```

Expected: clean tree, still on `ca26e24 Rewritten Wisdoms Objective Fix`, no `docs/` directory. If anything was written there, revert it — the main repository must stay free of AI docs and planning artefacts.

- [ ] **Step 8: Report**

State, per spec §9 item, whether it passed: step 1 (regression gate), step 2 (Layer 1), step 7 (charge round-trip). Report failures with the exact symptom rather than a summary.

No commit — this task changes no tracked file.

---

## What this plan does not fix

Stated so the next reader does not assume otherwise:

- **B2** — `CorruptionEffects.PlayerConditionID` is still only re-evaluated on rating change, never on area change. Spec §5.1 C3.
- **B3** — `Player::UpdateCorruption()` still re-applies live penalty auras on every rating change. Spec §5.1 C2.
- **B4** — no corruption spell scripts exist. Threshold penalties (Grasping Tendrils, Eye of Corruption, Grand Delusions, Cascading Disaster, Inevitable Doom, Inescapable Consequences) and the stateful item effects (Echoing Void, Infinite Stars, Twilight Devastation, Twisted Appendage, Void Ritual) have no mechanics behind them. Spec §5.2 and §6.2.
- **Bonus-granted on-use cooldowns** — `SpellHistory::GetCooldownDurations` receives an item id, so a bonus-granted effect cannot override its cooldown. Harmless for corruption, which is passive.

After this plan, the stat corruptions work end to end. The proc and summon corruptions have their auras applied but no mechanics; they are Layer 3.
