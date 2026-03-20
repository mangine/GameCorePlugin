# Alignment System — Architecture

**Part of:** GameCore Plugin | **Module:** `GameCore` | **UE Version:** 5.7

The Alignment System tracks one or more independent alignment axes per player. Each axis is defined by a data asset with a configurable effective range and a hysteresis (saturation) buffer. Mutations are applied in server-authoritative batches, validated per-axis via the Requirement System, and broadcast as a single event per batch through the Event Bus.

The system is fully decoupled — it has no knowledge of quests, skills, combat, or any other game system. It receives increments and decrements and emits events.

---

## Dependencies

### Unreal Engine Modules

| Module | Reason |
|---|---|
| `GameplayTags` | Axis identification via `FGameplayTag` |
| `GameplayMessageRuntime` | `UGameplayMessageSubsystem` (used internally by Event Bus) |
| `NetCore` | `FFastArraySerializer` for replication |

### GameCore Plugin Systems

| System | Usage |
|---|---|
| **Event Bus** (`UGameCoreEventBus`) | Broadcasts `FAlignmentChangedMessage` after each batch. Server-only scope. |
| **Requirement System** (`URequirementList`, `FRequirementContext`) | Per-axis change gate on `UAlignmentDefinition::ChangeRequirements`. |
| **Serialization System** (`IPersistableComponent`) | `UAlignmentComponent` implements `IPersistableComponent` to participate in the persistence lifecycle. |

### Build.cs

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameCore",
    "GameplayTags",
    "GameplayMessageRuntime",
    "NetCore",
});
```

---

## Requirements

- Multiple independent axes, each defined by a `UAlignmentDefinition` data asset.
- Hysteresis (saturation) buffer per axis: underlying value accumulates in a wider saturated range; effective value is clamped to a tighter effective range.
- All mutations are server-authoritative. The component lives on `APlayerState`.
- Mutation entry point is `ApplyAlignmentDeltas` — batch-only, preventing partial-application bugs.
- Per-axis requirement check via `URequirementList`. A failing axis is skipped; the rest of the batch continues.
- One `FAlignmentChangedMessage` broadcast per batch, containing only changed axes.
- Clients observe alignment changes through `FFastArraySerializer` replication (`COND_OwnerOnly`).
- Underlying values are persistable via `IPersistableComponent`.

---

## Features

- **Tag-driven axes.** Any number of axes; system has no hardcoded axis knowledge.
- **Hysteresis buffer.** Provides alignment momentum: reaching the effective ceiling requires sustained opposite behavior to start moving back.
- **Batch mutations.** Single atomic batch guarantees one GMS broadcast per logical action regardless of how many axes it touches.
- **Per-axis requirement gates.** Requirements evaluated server-side per axis from the definition asset.
- **Client replication.** `FFastArraySerializer` ensures only dirty items travel over the wire.
- **Client-side effective query.** Effective range is stored on `FAlignmentEntry` at registration time, enabling `GetEffectiveAlignment` on clients without access to `Definitions`.
- **Persistence.** Implements `IPersistableComponent` — underlying values are saved and restored via the serialization system.
- **Blueprint-friendly.** Full `BlueprintCallable` and `BlueprintPure` exposure.

---

## Design Decisions

| Decision | Rationale |
|---|---|
| Effective range replicated on `FAlignmentEntry` | `Definitions` map is server-only (populated via `RegisterAlignment`). Clients need the effective clamp range to compute `GetEffectiveAlignment`. Storing `EffectiveMin/Max` directly on `FAlignmentEntry` eliminates the need to replicate definitions or hold definition asset references on clients. |
| `UAlignmentDefinition` is a `UPrimaryDataAsset` | Designer-friendly, async-loadable, never owned per-player. |
| Requirements on the definition, not the call site | All rules for an axis live in one place. Call sites remain clean. |
| `FFastArraySerializer` with `COND_OwnerOnly` | Only dirty items travel over the wire. Only the owning player needs their own alignment data. |
| Definitions not replicated | Data assets load identically on all machines. Only live values need replication. |
| GMS scope `ServerOnly` | Clients react via replication. No client-side GMS broadcast needed. |
| `FRequirementContext` passed by caller | `UAlignmentComponent` has no knowledge of who triggered the change. The caller constructs context from its own domain. |
| Implements `IPersistableComponent` | Reuses the persistence system's lifecycle (dirty tracking, save/load, schema versioning) rather than rolling a bespoke save mechanism. |
| No partial rollback across axes | Each axis is independent by design. If axis 1 succeeds and axis 2 fails requirements, axis 1's change is kept. |

---

## Hysteresis — How It Works

```
SaturatedMin     EffectiveMin              EffectiveMax     SaturatedMax
    |--buffer--|------------ effective range ------------|--buffer--|
```

- The **underlying value** accumulates between `SaturatedMin` and `SaturatedMax`.
- The **effective value** = `Clamp(UnderlyingValue, EffectiveMin, EffectiveMax)`.
- When `UnderlyingValue > EffectiveMax`, effective stays at `EffectiveMax`. The player must drive the underlying value back below `EffectiveMax` before the effective value moves.
- Designers control buffer depth by setting how far `SaturatedMin/Max` extend beyond `EffectiveMin/Max`.

**Example:** `EffectiveMin = -100`, `EffectiveMax = 100`, `SaturatedMin = -200`, `SaturatedMax = 200`. A player at underlying = 200 must accumulate 100 points of "good" actions before the effective alignment starts moving from 100 toward 99.

---

## Logic Flow

```
Caller (server)
  │
  ├─ Constructs TArray<FAlignmentDelta>   (one or more axes)
  ├─ Constructs FRequirementContext        (Subject = PlayerState / APawn)
  └─ Calls UAlignmentComponent::ApplyAlignmentDeltas(Deltas, Context)
        │
        ├─ For each FAlignmentDelta:
        │     ├─ Skip if Delta == 0
        │     ├─ Lookup Definition by tag  → warn + skip if not registered
        │     ├─ Evaluate Def->ChangeRequirements (if set)  → skip axis if fail
        │     ├─ Add Delta to UnderlyingValue, clamp to [SaturatedMin, SaturatedMax]
        │     ├─ Compute AppliedDelta = new - old  → skip if zero (was saturated)
        │     └─ MarkItemDirty, append to Msg.Changes
        │
        └─ If Msg.Changes not empty:
              └─ UGameCoreEventBus::Broadcast(Alignment_Changed, Msg, ServerOnly)

FFastArraySerializer (COND_OwnerOnly)
  └─ Replicates dirty FAlignmentEntry items to owning client
        └─ OnRep_AlignmentData fires → OnAlignmentDataReplicated delegate

IPersistableComponent lifecycle
  └─ ApplyAlignmentDeltas calls NotifyDirty after any change
        ├─ UPersistenceRegistrationComponent::BuildPayload calls Serialize_Save
        └─ On load: Serialize_Load restores UnderlyingValue per axis
```

---

## Known Issues

| Issue | Status | Notes |
|---|---|---|
| `FindByTag` is O(n) | Known, acceptable | Axis count is bounded and tiny (2–5 axes per player). Not worth a `TMap` lookup for `FAlignmentEntry`. |
| `Definitions` server-only requires range duplication | By design | `EffectiveMin/Max` are stored on `FAlignmentEntry` at registration. Adds 8 bytes per entry. |
| No per-axis initial value support | Future | Currently always starts at `0`. If a game needs a non-zero initial value, `RegisterAlignment` must be updated to accept an optional initial value parameter. |

---

## File Structure

```
GameCore/Source/GameCore/
└── Alignment/
    ├── AlignmentDefinition.h           ← UAlignmentDefinition
    ├── AlignmentDefinition.cpp         ← IsDataValid implementation
    ├── AlignmentTypes.h                ← FAlignmentDelta, FAlignmentEntry, FAlignmentArray
    ├── AlignmentComponent.h            ← UAlignmentComponent declaration
    └── AlignmentComponent.cpp          ← UAlignmentComponent implementation
```

Event message structs are defined in:
```
GameCore/Source/GameCore/Alignment/AlignmentComponent.h
```
(Following the v2 convention: message structs live in the header of the system that owns them.)

Channel tags added to `DefaultGameplayTags.ini` inside the `GameCore` module.
Native tag handles added to `GameCoreEventTags.h` / `.cpp`.
