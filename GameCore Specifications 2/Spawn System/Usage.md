# Spawn System — Usage

---

## 1. Make an entity spawnable

Implement `ISpawnableEntity` on any `AActor` subclass. No other changes are required for the class to appear in the spawn entry picker.

```cpp
// MyNPC.h
UCLASS()
class UMyNPC : public ACharacter, public ISpawnableEntity
{
    GENERATED_BODY()
public:
    // Optional — override to receive spawn context.
    virtual void OnSpawnedByManager_Implementation(AActor* Anchor) override;

private:
    UPROPERTY()
    TObjectPtr<UMyLootComponent> LootComp;
};

// MyNPC.cpp
void UMyNPC::OnSpawnedByManager_Implementation(AActor* Anchor)
{
    if (!Anchor) return;

    USpawnManagerComponent* Mgr = Anchor->FindComponentByClass<USpawnManagerComponent>();
    if (!Mgr) return;

    // Apply the loot table override if the spawner configured one for this class.
    TSoftObjectPtr<ULootTable> Override = Mgr->GetLootTableOverrideForClass(GetClass());
    if (!Override.IsNull())
        LootComp->SetLootTableOverride(Override);
}
```

In **Blueprint**: implement the `ISpawnableEntity` interface on any Actor Blueprint via the *Class Settings → Implemented Interfaces* panel. Override the `OnSpawnedByManager` event node if post-spawn setup is needed.

---

## 2. Set up a spawn anchor actor in the level

1. Create a plain `AActor` Blueprint (e.g. `BP_SpawnAnchor_DockArea`).
2. Add a **`USpawnManagerComponent`** to it.
3. Configure the component in the Details panel:

### Required

| Property | Description |
|---|---|
| `SpawnEntries` | Add one entry per entity type. Each entry needs an `EntityClass` (must implement `ISpawnableEntity`) and a `MaxCount`. |
| `BaseFlowInterval` | Seconds between spawn ticks (min 10 s). |
| `GlobalFlowCount` | Max total spawns per tick across all entries. |
| `SpawnPointConfig` | Select a strategy (see below). |

### Spawn point strategies

**`USpawnPointConfig_RadiusRandom`** — random navmesh point within a radius:
- `Radius` — search radius in cm.
- `CenterComponentTag` — (optional) tag of a child `USceneComponent` to use as center; defaults to actor root.
- `MaxProjectionAttempts` — number of navmesh projection tries before giving up (default 3).

**`USpawnPointConfig_PointList`** — explicit point list from tagged child components:
1. Add child `USceneComponent`s to the anchor actor in the editor.
2. Set a shared `Component Tag` on each (e.g. `SpawnPoint`).
3. List those tags in `PointComponentTags`.
4. Choose `Selection` mode: `Random` or `RoundRobin`.

---

## 3. Gate entry spawns with requirements

Create a `URequirementList` asset and assign it to `FSpawnEntry::SpawnRequirements`. The requirement list is evaluated once per flow tick against a **world-state-only context** (no `PlayerState`). Use requirements such as `URequirement_TimeOfDay` or `URequirement_GameplayTag`.

```
// Example: spawn Lantern Crabs only at night
SpawnEntries[0].SpawnRequirements = RequirementList_NightOnly
```

> **Important:** Requirements that expect a non-null `PlayerState` will always fail on a spawn entry. This misconfiguration is caught by `ValidateRequirements` in non-Shipping builds.

---

## 4. Enable player-density scaling

```cpp
// In the component Details panel:
bScaleByNearbyPlayers = true
PlayerScanRadius      = 3000.f   // cm
PlayerCountForMinInterval = 5
MinFlowInterval       = 10.f
```

Then inject the counter delegate from your game module (bind once at world init or when the anchor actor initialises):

```cpp
// GameMode.cpp or a server-side subsystem
void AMyGameMode::OnSpawnAnchorReady(USpawnManagerComponent* Mgr)
{
    Mgr->OnCountNearbyPlayers = [this](FVector Location, float Radius) -> int32
    {
        TArray<FOverlapResult> Overlaps;
        FCollisionShape Sphere = FCollisionShape::MakeSphere(Radius);
        GetWorld()->OverlapMultiByObjectType(
            Overlaps, Location, FQuat::Identity,
            FCollisionObjectQueryParams(ECC_Pawn), Sphere);

        int32 Count = 0;
        for (const FOverlapResult& O : Overlaps)
        {
            if (O.GetActor() && O.GetActor()->IsA<APlayerPawn>())
                ++Count;
        }
        return Count;
    };
}
```

> If `OnCountNearbyPlayers` is left unbound while `bScaleByNearbyPlayers` is true, a one-time `UE_LOG Warning` is emitted and the interval falls back to `BaseFlowInterval`.

---

## 5. Use a loot table override

Set `FSpawnEntry::LootTableOverride` in the Details panel. The spawner passes the soft reference to the entity via `OnSpawnedByManager`. The entity applies it:

```cpp
void UMyNPC::OnSpawnedByManager_Implementation(AActor* Anchor)
{
    USpawnManagerComponent* Mgr = Anchor
        ? Anchor->FindComponentByClass<USpawnManagerComponent>()
        : nullptr;
    if (!Mgr) return;

    TSoftObjectPtr<ULootTable> Override = Mgr->GetLootTableOverrideForClass(GetClass());
    if (!Override.IsNull())
        LootComp->SetLootTableOverride(Override);
    // Entity is responsible for async-loading the loot table if needed.
}
```

The spawner never loads or holds a hard reference to the loot table asset.

---

## 6. Add a custom spawn point strategy

```cpp
// MySpawnPointConfig_Formation.h
UCLASS(EditInlineNew, DisplayName = "Formation")
class UMySpawnPointConfig_Formation : public USpawnPointConfig
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category = "SpawnPoint")
    TArray<FVector> FormationOffsets;

    virtual bool ResolveSpawnTransform(
        AActor* AnchorActor,
        FTransform& OutTransform) const override;

private:
    mutable int32 NextIndex = 0;
};

bool UMySpawnPointConfig_Formation::ResolveSpawnTransform(
    AActor* AnchorActor, FTransform& OutTransform) const
{
    if (!AnchorActor || FormationOffsets.IsEmpty()) return false;
    const FVector Origin = AnchorActor->GetActorLocation();
    OutTransform = FTransform(Origin + FormationOffsets[NextIndex % FormationOffsets.Num()]);
    ++NextIndex;
    return true;
}
```

No changes to `USpawnManagerComponent` are needed. Select the new strategy in the `SpawnPointConfig` instanced property dropdown.

---

## 7. Implicit entry priority

Entry order in `SpawnEntries` is **implicit spawn priority** when the global budget runs out mid-array. Put the most critical entity types first. Example:

```
SpawnEntries[0] — BossGuard   MaxCount=1  MaxPerTick=1   (highest priority)
SpawnEntries[1] — Soldier     MaxCount=4  MaxPerTick=2
SpawnEntries[2] — Ambient     MaxCount=6  MaxPerTick=2   (lowest priority)
```

If `GlobalFlowCount = 3` and both BossGuard and Soldier have vacancies, BossGuard is attempted first and consumes 1 budget slot; Soldier gets the remaining 2.
