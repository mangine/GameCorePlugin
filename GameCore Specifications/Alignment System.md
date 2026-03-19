# Alignment System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The Alignment System tracks one or more independent alignment axes per player. Each axis is defined by a data asset and has a configurable effective range and a hysteresis (saturation) buffer. Changes are applied in batches, validated server-side against per-axis requirements, and broadcast as a single GMS event per batch.

The system is fully decoupled — it has no knowledge of quests, skills, combat, or any other game system. It receives increments and decrements and emits events. Nothing else.

---

## Design Requirements

- **Multiple independent axes.** The developer defines any number of alignment axes (e.g. `Alignment.GoodEvil`, `Alignment.LawChaos`). Each axis is independent — changing one never affects another.
- **Hysteresis (saturation) buffer.** Every axis has an *underlying* value that accumulates freely within a saturated range (`SaturatedMin` / `SaturatedMax`). The *effective* value returned to consumers is clamped to a tighter range (`EffectiveMin` / `EffectiveMax`). The gap between the two ranges acts as a momentum buffer: a player at effective maximum must push the underlying value back into the effective range before the effective value moves.
- **Batch mutations only.** `ApplyAlignmentDeltas` is the sole mutation entry point. A single-axis change is a batch of one. This prevents partial-application bugs and allows one GMS broadcast per logical action regardless of how many axes it touches.
- **Per-axis requirements.** Each axis definition may reference a `URequirementList`. If the requirements fail for a given axis, that axis is skipped — the rest of the batch continues.
- **One GMS event per batch.** A single `FAlignmentChangedMessage` is broadcast after all axes are processed, containing only the axes that actually changed. Axes that were skipped (requirements failed, delta was zero, or already saturated) are not included.
- **Server-authoritative.** All mutations run on the server. `UAlignmentComponent` lives on `APlayerState` and replicates via `FFastArraySerializer`. Clients observe changes through replication, not through direct mutation.

---

## Key Decisions

| Decision | Rationale |
|---|---|
| Underlying value is not replicated separately from effective | Effective value is derived at query time from underlying + definition. No extra replicated property, no sync risk. |
| `UAlignmentDefinition` is a `UPrimaryDataAsset` | Designer-friendly, supports async loading, referenced by pointer — definitions are never owned per-player. |
| Requirements are on the definition, not the call site | Keeps mutation call sites clean. All rules for an axis live in one place. |
| `FFastArraySerializer` for runtime data | Only dirty items travel over the wire. In practice only 1–2 axes change per action — minimal bandwidth. |
| Definitions are not replicated | Data assets load identically on all machines. Only live values need replication. |
| GMS scope is `ServerOnly` | Clients react via `FFastArraySerializer` replication. No client-side GMS broadcast needed. |
| `FRequirementContext` is passed by the caller | `UAlignmentComponent` has no knowledge of who triggered the change. The caller constructs context from its own domain (player state, pawn, etc.). |

---

## System Modules

| Module | Classes | Role |
|---|---|---|
| **Data** | `UAlignmentDefinition` | Per-axis designer config — ranges, requirements |
| **Runtime Types** | `FAlignmentDelta`, `FAlignmentEntry`, `FAlignmentArray` | Batch input, per-player live data, replicated container |
| **Component** | `UAlignmentComponent` | Owns runtime data, executes batch mutations, fires GMS event |
| **GMS Event** | `FAlignmentChangedMessage`, `FAlignmentChangedEntry` | One broadcast per batch, N changed axes |

---

## File and Folder Structure

```
GameCore/Source/GameCore/
└── Alignment/
    ├── AlignmentDefinition.h              ← UAlignmentDefinition
    ├── AlignmentTypes.h                   ← FAlignmentDelta, FAlignmentEntry, FAlignmentArray
    ├── AlignmentComponent.h / .cpp        ← UAlignmentComponent
```

GMS message structs added to the existing:
```
GameCore/Source/GameCore/EventBus/GameCoreEventMessages.h
```

Channel tags added to `DefaultGameplayTags.ini` inside the `GameCore` module.

---

## Hysteresis — How It Works

```
SaturatedMin          EffectiveMin       EffectiveMax          SaturatedMax
    |-------- buffer -------|--------- effective ---------|-------- buffer --------|
```

- The **underlying value** accumulates between `SaturatedMin` and `SaturatedMax`.
- The **effective value** = `Clamp(underlying, EffectiveMin, EffectiveMax)`.
- When `underlying > EffectiveMax` (player pushed past the ceiling), effective stays at `EffectiveMax`. The player must accumulate enough negative delta to bring `underlying` back below `EffectiveMax` before the effective value moves down.
- Designers control the buffer depth by setting how far `SaturatedMin/Max` extend beyond `EffectiveMin/Max`.

**Example:** `EffectiveMin = -100`, `EffectiveMax = 100`, `SaturatedMin = -200`, `SaturatedMax = 200`. A player at underlying = 200 (maximum evil) must accumulate +100 good actions before effective alignment starts moving from 100 toward 99.

---

## GMS Integration

**Channel:** `GameCoreEvent.Alignment.Changed`
**Scope:** `ServerOnly`
**One broadcast per `ApplyAlignmentDeltas` call**, containing all axes that changed.

Clients observe alignment changes through replicated `FAlignmentArray` — no client-side GMS broadcast is fired.

---

## Module Dependencies

```csharp
// AlignmentComponent Build.cs
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameCore",          // URequirementList, UGameCoreEventSubsystem
    "GameplayTags",
    "GameplayMessageRuntime",
    "NetCore",           // FFastArraySerializer
});
```

---

## Quick-Start: Adding and Using Alignment

### 1. Create an alignment definition asset

In the editor: right-click → **GameCore → Alignment Definition**. Set the tag, ranges, and optionally a `ChangeRequirements` list.

### 2. Register the axis on the server

```cpp
// In AMyPlayerState::BeginPlay or a game-mode setup function (server only)
if (UAlignmentComponent* Comp = FindComponentByClass<UAlignmentComponent>())
{
    Comp->RegisterAlignment(GoodEvilDefinition);   // e.g. tag = Alignment.GoodEvil
    Comp->RegisterAlignment(LawChaosDefinition);   // e.g. tag = Alignment.LawChaos
}
```

### 3. Apply deltas (server-side, e.g. from a quest or combat system)

```cpp
FRequirementContext Ctx;
Ctx.Subject = PlayerState;

TArray<FAlignmentDelta> Deltas;
Deltas.Add({ AlignmentTags::GoodEvil, -25.f });  // Moves toward evil
Deltas.Add({ AlignmentTags::LawChaos, +10.f });  // Moves toward lawful

AlignmentComp->ApplyAlignmentDeltas(Deltas, Ctx);
```

### 4. Query alignment (server or client)

```cpp
float Evil = AlignmentComp->GetEffectiveAlignment(AlignmentTags::GoodEvil);
// Returns value clamped to [EffectiveMin, EffectiveMax]

float RawEvil = AlignmentComp->GetUnderlyingAlignment(AlignmentTags::GoodEvil);
// Returns raw accumulated value — use for serialization or debug
```

### 5. Listen for alignment changes

```cpp
void UMyQuestTracker::BeginPlay()
{
    if (UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this))
    {
        AlignmentHandle = Bus->StartListening<FAlignmentChangedMessage>(
            GameCoreEventTags::Alignment_Changed,
            this, &UMyQuestTracker::OnAlignmentChanged);
    }
}

void UMyQuestTracker::OnAlignmentChanged(FGameplayTag Channel, const FAlignmentChangedMessage& Msg)
{
    for (const FAlignmentChangedEntry& Entry : Msg.Changes)
    {
        if (Entry.AlignmentTag == AlignmentTags::GoodEvil)
        {
            // React to good/evil change
        }
    }
}
```

---

## Sub-Pages

| Sub-Page | Covers |
|---|---|
| [UAlignmentDefinition](Alignment%20System/UAlignmentDefinition.md) | Data asset — axis config, ranges, requirements reference |
| [Runtime Types](Alignment%20System/Runtime%20Types.md) | `FAlignmentDelta`, `FAlignmentEntry`, `FAlignmentArray` — batch input and replicated container |
| [UAlignmentComponent](Alignment%20System/UAlignmentComponent.md) | Component — registration, mutation, query, replication |
| [GMS Event Messages](Alignment%20System/GMS%20Event%20Messages.md) | `FAlignmentChangedMessage`, channel tags, integration rules |
