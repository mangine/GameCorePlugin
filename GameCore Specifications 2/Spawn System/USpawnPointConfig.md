# USpawnPointConfig

**Module:** `GameCore`  
**File:** `GameCore/Source/GameCore/Spawning/SpawnPointConfig.h / .cpp`

Abstract `UObject`-based strategy hierarchy that resolves a world-space spawn transform for a given attempt. `USpawnManagerComponent` holds one instanced `USpawnPointConfig` and calls `ResolveSpawnTransform` per spawn attempt. The component has no switch/case — all strategy differences live inside subclasses.

Two concrete implementations ship: `USpawnPointConfig_RadiusRandom` and `USpawnPointConfig_PointList`.

---

## Base Class

```cpp
// File: GameCore/Source/GameCore/Spawning/SpawnPointConfig.h

/**
 * Abstract spawn location strategy. Subclass and override ResolveSpawnTransform
 * to implement a new placement approach.
 *
 * Instances are owned by USpawnManagerComponent via EditInlineNew.
 * Must be stateless with respect to individual spawn attempts —
 * no per-spawn state may be stored (round-robin index is stable instance
 * state, not per-call transient state).
 */
UCLASS(Abstract, EditInlineNew, CollapseCategories, BlueprintType, Blueprintable)
class GAMECORE_API USpawnPointConfig : public UObject
{
    GENERATED_BODY()
public:
    /**
     * Attempt to resolve a world-space transform for one spawn attempt.
     *
     * @param AnchorActor  The USpawnManagerComponent's owner.
     * @param OutTransform Filled on success. Callers may inspect it on failure
     *                     but the value is unspecified.
     * @return             True if a valid transform was resolved; false to skip
     *                     this spawn attempt silently (retry next natural tick).
     */
    virtual bool ResolveSpawnTransform(
        AActor*     AnchorActor,
        FTransform& OutTransform) const
        PURE_VIRTUAL(USpawnPointConfig::ResolveSpawnTransform, return false;);

protected:
    /**
     * Returns the first child USceneComponent on AnchorActor whose
     * ComponentTags contains ComponentTag. Returns nullptr if no match.
     */
    static USceneComponent* FindChildComponentByTag(
        AActor* AnchorActor,
        FName   ComponentTag);

    /**
     * Collects all child USceneComponents on AnchorActor whose
     * ComponentTags contains ComponentTag. OutComponents is appended to,
     * not reset — callers must reset before calling if needed.
     */
    static void CollectChildComponentsByTag(
        AActor*                   AnchorActor,
        FName                     ComponentTag,
        TArray<USceneComponent*>& OutComponents);
};
```

### Helper Implementations

```cpp
// SpawnPointConfig.cpp

USceneComponent* USpawnPointConfig::FindChildComponentByTag(
    AActor* AnchorActor, FName ComponentTag)
{
    if (!AnchorActor || ComponentTag.IsNone()) return nullptr;
    TArray<USceneComponent*> All;
    AnchorActor->GetComponents<USceneComponent>(All);
    for (USceneComponent* Comp : All)
    {
        if (Comp && Comp->ComponentTags.Contains(ComponentTag))
            return Comp;
    }
    return nullptr;
}

void USpawnPointConfig::CollectChildComponentsByTag(
    AActor* AnchorActor, FName ComponentTag,
    TArray<USceneComponent*>& OutComponents)
{
    if (!AnchorActor || ComponentTag.IsNone()) return;
    TArray<USceneComponent*> All;
    AnchorActor->GetComponents<USceneComponent>(All);
    for (USceneComponent* Comp : All)
    {
        if (Comp && Comp->ComponentTags.Contains(ComponentTag))
            OutComponents.Add(Comp);
    }
}
```

> **Note:** `CollectChildComponentsByTag` **appends** to `OutComponents` (does not reset). This is intentional — `USpawnPointConfig_PointList` calls it once per tag to build a combined candidate list without an intermediate TArray copy.

---

## ESpawnPointSelection

```cpp
// File: GameCore/Source/GameCore/Spawning/SpawnPointConfig.h

UENUM(BlueprintType)
enum class ESpawnPointSelection : uint8
{
    Random     UMETA(DisplayName = "Random"),
    RoundRobin UMETA(DisplayName = "Round Robin"),
};
```

---

## USpawnPointConfig_RadiusRandom

Picks a random navmesh point within a sphere of `Radius` centred on a configurable origin. If `CenterComponentTag` is set, the origin is the world location of the first matching child `USceneComponent`; otherwise the anchor actor's root location is used.

```cpp
UCLASS(DisplayName = "Radius Random")
class GAMECORE_API USpawnPointConfig_RadiusRandom : public USpawnPointConfig
{
    GENERATED_BODY()
public:
    /**
     * Tag of a child USceneComponent to use as the search centre.
     * If None or no matching component is found, the anchor actor's root is used.
     */
    UPROPERTY(EditAnywhere, Category = "SpawnPoint")
    FName CenterComponentTag;

    /** Search radius in cm. Must be > 0. */
    UPROPERTY(EditAnywhere, Category = "SpawnPoint", meta = (ClampMin = 50.f))
    float Radius = 500.f;

    /**
     * Number of random point attempts before giving up and returning false.
     * Each attempt picks a new random XY offset and projects onto the navmesh.
     * Increase if the navmesh is sparse in this area.
     */
    UPROPERTY(EditAnywhere, Category = "SpawnPoint", meta = (ClampMin = 1, ClampMax = 10))
    int32 MaxProjectionAttempts = 3;

    virtual bool ResolveSpawnTransform(
        AActor*     AnchorActor,
        FTransform& OutTransform) const override;
};
```

### ResolveSpawnTransform — RadiusRandom

```cpp
bool USpawnPointConfig_RadiusRandom::ResolveSpawnTransform(
    AActor* AnchorActor, FTransform& OutTransform) const
{
    if (!AnchorActor) return false;

    FVector Center = AnchorActor->GetActorLocation();
    if (!CenterComponentTag.IsNone())
    {
        if (USceneComponent* Comp = FindChildComponentByTag(AnchorActor, CenterComponentTag))
            Center = Comp->GetComponentLocation();
    }

    UNavigationSystemV1* NavSys =
        FNavigationSystem::GetCurrent<UNavigationSystemV1>(AnchorActor->GetWorld());
    if (!NavSys) return false;

    for (int32 i = 0; i < MaxProjectionAttempts; ++i)
    {
        const FVector2D RandDir = FMath::RandPointInCircle(Radius);
        const FVector Candidate = Center + FVector(RandDir.X, RandDir.Y, 0.f);

        FNavLocation NavLocation;
        if (NavSys->ProjectPointToNavigation(
                Candidate, NavLocation,
                FVector(Radius, Radius, 500.f))) // generous Z for sloped terrain
        {
            OutTransform = FTransform(NavLocation.Location);
            return true;
        }
    }
    return false;
}
```

---

## USpawnPointConfig_PointList

Maintains an explicit list of candidate spawn points defined as child `USceneComponent` tags on the anchor actor. Supports Random and RoundRobin selection.

```cpp
UCLASS(DisplayName = "Point List")
class GAMECORE_API USpawnPointConfig_PointList : public USpawnPointConfig
{
    GENERATED_BODY()
public:
    /**
     * Tags of child USceneComponents on the anchor actor.
     * All components matching any listed tag are pooled as candidates.
     * At least one tag must resolve to at least one component at runtime.
     */
    UPROPERTY(EditAnywhere, Category = "SpawnPoint")
    TArray<FName> PointComponentTags;

    UPROPERTY(EditAnywhere, Category = "SpawnPoint")
    ESpawnPointSelection Selection = ESpawnPointSelection::Random;

    virtual bool ResolveSpawnTransform(
        AActor*     AnchorActor,
        FTransform& OutTransform) const override;

private:
    // Round-robin index. Mutable — advances on each successful call.
    // Not a UPROPERTY — transient, never serialized.
    mutable int32 RoundRobinIndex = 0;
};
```

### ResolveSpawnTransform — PointList

```cpp
bool USpawnPointConfig_PointList::ResolveSpawnTransform(
    AActor* AnchorActor, FTransform& OutTransform) const
{
    if (!AnchorActor || PointComponentTags.IsEmpty()) return false;

    // Collect all candidate components across all listed tags.
    TArray<USceneComponent*> Candidates;
    for (const FName& Tag : PointComponentTags)
        CollectChildComponentsByTag(AnchorActor, Tag, Candidates);

    if (Candidates.IsEmpty()) return false;

    USceneComponent* Chosen = nullptr;
    if (Selection == ESpawnPointSelection::Random)
    {
        Chosen = Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
    }
    else // RoundRobin
    {
        RoundRobinIndex = RoundRobinIndex % Candidates.Num();
        Chosen = Candidates[RoundRobinIndex];
        ++RoundRobinIndex;
    }

    OutTransform = Chosen->GetComponentTransform();
    return true;
}
```

---

## Adding a New Strategy

1. Subclass `USpawnPointConfig` in the `GameCore` module.
2. Add `EditInlineNew` and a designer-friendly `DisplayName` to the `UCLASS` macro.
3. Declare config properties as `EditAnywhere`.
4. Override `ResolveSpawnTransform`. Return `false` on any unresolvable state.
5. No changes to `USpawnManagerComponent` are needed.

---

## Notes

- `ResolveSpawnTransform` is `const`. Per-attempt state must not be stored. `RoundRobinIndex` is `mutable` — it is stable instance state (not per-call transient state).
- The navmesh projection Z extent (`500.f`) may need tuning for extreme terrain. Consider exposing it as a designer property on `USpawnPointConfig_RadiusRandom` if the project has deep valleys near spawn areas.
- `USpawnPointConfig` subclasses live in the runtime module — no editor-only code required.
- The helper was renamed from `FindChildComponentsByTag` (original spec) to `CollectChildComponentsByTag` to clearly communicate its append semantics and avoid the dead-code bug present in the original `USpawnPointConfig_PointList` implementation.
