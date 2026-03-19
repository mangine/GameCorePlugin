# Spawn System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The Spawn System manages the continuous population of world entities (NPCs, mobs, ambient creatures, etc.) without coupling to any specific class. A single `USpawnManagerComponent` on an anchor actor drives all spawn logic for that location: it tracks live instances, rate-limits spawning via a flow timer, evaluates per-entry requirements, and scales the flow rate based on nearby player count when configured.

---

## Sub-pages

- [ISpawnableEntity](Spawn%20System/ISpawnableEntity.md)
- [USpawnPointConfig](Spawn%20System/USpawnPointConfig.md)
- [FSpawnEntry](Spawn%20System/FSpawnEntry.md)
- [USpawnManagerComponent](Spawn%20System/USpawnManagerComponent.md)

---

## Requirements

| # | Requirement |
|---|---|
| R1 | The component must manage multiple entity types simultaneously, each with its own max live count. |
| R2 | A global flow timer controls how often the system attempts to spawn. The base interval must always be >= 10 seconds. |
| R3 | A global `FlowCount` caps the total number of spawns per flow tick across all entries. |
| R4 | Each entry may optionally declare a `MaxPerTick` cap limiting how many of that specific type spawn per tick. |
| R5 | A `bScaleByNearbyPlayers` flag enables dynamic interval scaling. Player count is sampled at each flow tick expiry (not on a separate tick). Interval lerps from `BaseFlowInterval` down to `MinFlowInterval` as player count increases. Both bounds are >= 10 s. |
| R6 | A jitter of `[0, 1]` seconds is added to every rescheduled timer to spread load across many simultaneous spawn managers. |
| R7 | The system must not couple to any specific entity class. Entities are selected via the `ISpawnableEntity` interface; the asset picker is filtered by this interface in the editor. |
| R8 | Each `FSpawnEntry` may reference a `URequirementList` that gates whether that entry is eligible to spawn on a given tick (e.g. time-of-day, world state). |
| R9 | Spawn location is resolved by a pluggable `USpawnPointConfig` strategy object (instanced on the component). Two concrete strategies ship: `USpawnPointConfig_RadiusRandom` and `USpawnPointConfig_PointList`. |
| R10 | Spawn point candidates are child `USceneComponent`s on the anchor actor identified by tag — not mesh sockets. |
| R11 | When a live entity is destroyed, its slot becomes vacant immediately via `OnDestroyed` binding. Weak pointer pruning at each flow tick is a safety net only. |
| R12 | Spawn failures (navmesh miss, collision block) are silent skips. The slot retries on the next natural flow tick. No dedicated retry timer. |
| R13 | Entity classes are `TSoftClassPtr`. Async load is queued on first encounter; entries not yet loaded are skipped on the current tick and attempted again once loaded. No blocking load on the game thread. |
| R14 | Each `FSpawnEntry` may optionally hold a `TSoftObjectPtr<ULootTable>` override. The spawner passes this to the entity post-spawn via `OnSpawnedByManager`; it does not load or use it itself. |
| R15 | The system is server-only. `BeginPlay` returns immediately if `!HasAuthority`. No replication, no client path. |
| R16 | Spawn state (`LiveInstances`) is fully ephemeral and never persisted. Server restart repopulates naturally within the first few flow ticks. |
| R17 | Player counting is injected via a `TFunction` delegate (`OnCountNearbyPlayers`) bound by the game module. GameCore itself has no dependency on how "a player" is represented. If unbound and `bScaleByNearbyPlayers` is true, scaling is skipped and a one-time warning is logged. |

---

## Key Design Decisions

| Decision | Rationale |
|---|---|
| `ISpawnableEntity` marker interface, not a base class | Any actor class — regardless of its inheritance hierarchy — can be made spawnworthy with one line. Avoids forcing a common base that would conflict with existing actor hierarchies. |
| `USpawnPointConfig` as an instanced `UObject` hierarchy | Spawn location logic varies significantly between use cases. Polymorphic strategy objects keep each variant's properties and logic self-contained. Adding new strategies requires zero changes to the component. |
| Child `USceneComponent` tags, not root mesh sockets | Socket points require artist cooperation and a mesh on the anchor actor. Scene component tags are 100% designer-driven; any number of points can be added to any anchor actor in the editor without code changes. |
| Global `FlowCount` budget across all entries | Prevents a single entry with many vacancies from monopolizing all spawn budget on a tick. Entry ordering in `SpawnEntries` acts as implicit priority. |
| `MaxPerTick` per entry alongside global budget | Gives designers fine-grained control over how aggressively a single entry type fills its vacancies per tick, without removing the global budget safety. |
| Reschedule timer before spawn work | Spawn attempts can fail or block briefly on async loads. Rescheduling first ensures the timer period is not corrupted by work duration. |
| Spawn failure = silent skip, retry next natural tick | A dedicated retry timer risks hammering a glitched location every few frames. Natural retry spacing is safe and good enough. |
| `OnDestroyed` delegate + weak pointer prune | `OnDestroyed` handles the common case with zero polling. Weak pointer pruning on each tick is a cheap safety net for actors destroyed via means that bypass the delegate (e.g. `DestroyActor` from subsystems that clear all actors). |
| Per-entry `URequirementList`, not inline requirements | Shared requirement list assets let multiple entries (and even multiple components) reference the same condition without duplicating it. Consistent with how requirements are used across all other GameCore systems. |
| `OnCountNearbyPlayers` as injected delegate | GameCore cannot assume how the game represents players. The delegate pattern keeps the system fully reusable across projects. |
| `MinFlowInterval` hard-clamped to >= 10 s at runtime | Designer input is never trusted blindly. The floor prevents accidental near-zero intervals that would DoS the spawn system under high player load. |
| No persistence | Spawn state is intentionally ephemeral. NPCs and mobs are respawnable world state and must never register with `UPersistenceSubsystem`. Consistent with the established persistence design principle. |

---

## File and Folder Structure

```
GameCore/Source/GameCore/Spawning/
  ISpawnableEntity.h
  SpawnPointConfig.h / .cpp
  SpawnEntry.h
  SpawnManagerComponent.h / .cpp

GameCoreEditor/Spawning/
  FSpawnEntryCustomization.h / .cpp
```

---

## Quick Integration Guide

### 1. Make an entity spawnable

```cpp
// MyNPC.h
UCLASS()
class UMyNPC : public ACharacter, public ISpawnableEntity
{
    GENERATED_BODY()
public:
    // Optional override — called after the manager spawns this actor.
    virtual void OnSpawnedByManager_Implementation(AActor* Anchor) override;
};

// MyNPC.cpp
void UMyNPC::OnSpawnedByManager_Implementation(AActor* Anchor)
{
    // Set faction, loot table override, AI tree, etc.
    // Anchor is the spawn manager's owner actor.
}
```

### 2. Set up an anchor actor in the level

1. Create a new `AActor` Blueprint (e.g. `BP_SpawnAnchor_DockArea`).
2. Add `USpawnManagerComponent` to it.
3. In the component's Details panel:
   - Add entries to `SpawnEntries`. Each entry references a `TSoftClassPtr` to a Blueprint that implements `ISpawnableEntity`.
   - Set `MaxCount` and (optionally) `MaxPerTick` per entry.
   - Set `BaseFlowInterval`, `GlobalFlowCount`.
   - Assign a `SpawnPointConfig`:
     - `USpawnPointConfig_RadiusRandom`: set `Radius`. Optionally set `CenterComponentTag` to center on a child Scene Component instead of the actor root.
     - `USpawnPointConfig_PointList`: add child `USceneComponent`s to the anchor actor, set their Component Tag to a shared value (e.g. `SpawnPoint`), then list that tag in `PointComponentTags`.
4. To use time-of-day gating, create a `URequirementList` asset with e.g. `URequirement_TimeOfDay` and assign it to `FSpawnEntry::SpawnRequirements`.

### 3. Inject the player counter (game module, server)

```cpp
// In your GameMode or a server-side subsystem:
void AMyGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    // Find all spawn managers and inject the delegate.
    // In practice, wire this once at world start or when the spawn manager registers.
}

// When configuring a USpawnManagerComponent (e.g. in BeginPlay of the anchor actor):
USpawnManagerComponent* SpawnMgr = FindComponentByClass<USpawnManagerComponent>();
if (SpawnMgr)
{
    SpawnMgr->OnCountNearbyPlayers = [](FVector Location, float Radius) -> int32
    {
        // Use your game's player tracking (pawn overlap, player state list, etc.)
        TArray<FOverlapResult> Overlaps;
        FCollisionShape Sphere = FCollisionShape::MakeSphere(Radius);
        GetWorld()->OverlapMultiByObjectType(
            Overlaps, Location, FQuat::Identity,
            FCollisionObjectQueryParams(ECC_Pawn), Sphere);

        int32 Count = 0;
        for (const FOverlapResult& O : Overlaps)
        {
            if (O.GetActor() && O.GetActor()->IsA<ACharacter>())
                Count++;
        }
        return Count;
    };
}
```

### 4. LootTable override flow

If `FSpawnEntry::LootTableOverride` is set, the spawn manager passes it to the entity via `OnSpawnedByManager`. The entity is responsible for receiving and applying it:

```cpp
// In ISpawnableEntity implementor:
void UMyNPC::OnSpawnedByManager_Implementation(AActor* Anchor)
{
    USpawnManagerComponent* Mgr = Anchor->FindComponentByClass<USpawnManagerComponent>();
    if (!Mgr) return;

    // Find the entry that spawned this actor and read its loot table override.
    // Convention: Mgr exposes GetLootTableOverrideForClass(TSubclassOf<AActor>).
    TSoftObjectPtr<ULootTable> Override = Mgr->GetLootTableOverrideForClass(GetClass());
    if (!Override.IsNull())
        MyLootComponent->SetLootTableOverride(Override);
}
```
