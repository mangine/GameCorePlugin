# USpawnManagerComponent

**Module:** `GameCore`
**File:** `GameCore/Source/GameCore/Spawning/SpawnManagerComponent.h / .cpp`

Server-only `UActorComponent`. Placed on a spawn anchor actor. Drives the full spawn lifecycle: async class loading, flow-timer scheduling, per-entry requirement evaluation, spawn attempts, live-instance tracking, and player-count-based interval scaling.

---

## Class Declaration

```cpp
// File: GameCore/Source/GameCore/Spawning/SpawnManagerComponent.h

UCLASS(ClassGroup = "GameCore", meta = (BlueprintSpawnableComponent))
class GAMECORE_API USpawnManagerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USpawnManagerComponent();

    // ── Entries ───────────────────────────────────────────────────────────────

    /**
     * Entity types this manager keeps alive.
     * Entry order = implicit spawn priority when the global budget is exhausted
     * mid-array. Put highest-priority entities first.
     */
    UPROPERTY(EditAnywhere, Category = "Spawning")
    TArray<FSpawnEntry> SpawnEntries;

    // ── Flow Control ──────────────────────────────────────────────────────────

    /**
     * Base seconds between flow ticks. Always >= 10 s at runtime regardless
     * of the designer-set value. Actual interval = ComputeNextInterval().
     */
    UPROPERTY(EditAnywhere, Category = "Spawning|Flow", meta = (ClampMin = 10.f))
    float BaseFlowInterval = 30.f;

    /**
     * Maximum total spawns across all entries per flow tick.
     * When exhausted mid-array, remaining entries are skipped until next tick.
     */
    UPROPERTY(EditAnywhere, Category = "Spawning|Flow", meta = (ClampMin = 1))
    int32 GlobalFlowCount = 1;

    // ── Spawn Point ───────────────────────────────────────────────────────────

    /**
     * Instanced strategy object that resolves world-space spawn transforms.
     * Must not be null at BeginPlay — a warning is logged and a default
     * USpawnPointConfig_RadiusRandom (Radius=500) is created if missing.
     */
    UPROPERTY(EditAnywhere, Instanced, Category = "Spawning|SpawnPoint")
    TObjectPtr<USpawnPointConfig> SpawnPointConfig;

    // ── Player Multiplier ─────────────────────────────────────────────────────

    /**
     * If true, the flow interval is scaled down based on nearby player count.
     * Player count is sampled once at each flow tick expiry — not on a separate timer.
     * Requires OnCountNearbyPlayers to be bound by the game module.
     */
    UPROPERTY(EditAnywhere, Category = "Spawning|PlayerMultiplier")
    bool bScaleByNearbyPlayers = false;

    /**
     * Overlap sphere radius (cm) for the nearby-player scan.
     * Only used when bScaleByNearbyPlayers is true.
     */
    UPROPERTY(EditAnywhere, Category = "Spawning|PlayerMultiplier",
        meta = (EditCondition = "bScaleByNearbyPlayers", ClampMin = 100.f))
    float PlayerScanRadius = 2000.f;

    /**
     * Player count at which the flow interval reaches MinFlowInterval.
     * Counts above this value do not reduce the interval further.
     */
    UPROPERTY(EditAnywhere, Category = "Spawning|PlayerMultiplier",
        meta = (EditCondition = "bScaleByNearbyPlayers", ClampMin = 1))
    int32 PlayerCountForMinInterval = 5;

    /**
     * Minimum achievable flow interval (seconds) when player count is at or
     * above PlayerCountForMinInterval. Always >= 10 s at runtime.
     */
    UPROPERTY(EditAnywhere, Category = "Spawning|PlayerMultiplier",
        meta = (EditCondition = "bScaleByNearbyPlayers", ClampMin = 10.f))
    float MinFlowInterval = 10.f;

    /**
     * Injected by the game module. Called once per flow tick when
     * bScaleByNearbyPlayers is true.
     *
     * Signature: int32(FVector Location, float Radius)
     *   Location — anchor actor's world location
     *   Radius   — PlayerScanRadius
     *   Returns  — number of players within the radius
     *
     * If unbound and bScaleByNearbyPlayers is true, interval scaling is skipped
     * and a UE_LOG Warning is emitted once per component lifetime.
     */
    TFunction<int32(FVector, float)> OnCountNearbyPlayers;

    // ── Public API ────────────────────────────────────────────────────────────

    /**
     * Returns the soft loot table override for the given actor class,
     * scanning all SpawnEntries for a matching EntityClass.
     * Returns a null TSoftObjectPtr if no entry matches or no override is set.
     * Called by ISpawnableEntity implementors from OnSpawnedByManager.
     */
    UFUNCTION(BlueprintCallable, Category = "Spawning")
    TSoftObjectPtr<ULootTable> GetLootTableOverrideForClass(
        TSubclassOf<AActor> ActorClass) const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    // ── Flow Timer ────────────────────────────────────────────────────────────
    void OnFlowTimerExpired();
    void ScheduleNextFlowTick(int32 NearbyPlayers);
    float ComputeNextInterval(int32 NearbyPlayers) const;

    FTimerHandle FlowTimerHandle;

    // ── Spawn Attempt ─────────────────────────────────────────────────────────
    /**
     * Attempts to spawn one instance for the given entry.
     * Returns the spawned actor on success, nullptr on failure.
     * On success: binds OnDestroyed, calls OnSpawnedByManager, adds to LiveInstances.
     */
    AActor* TrySpawnForEntry(FSpawnEntry& Entry);

    /**
     * Bound to every spawned actor's OnDestroyed delegate.
     * Removes the actor from its entry's LiveInstances immediately.
     */
    UFUNCTION()
    void OnSpawnedActorDestroyed(AActor* DestroyedActor);

    // ── Async Class Loading ───────────────────────────────────────────────────
    /**
     * Queues async load for any entries whose EntityClass is not yet loaded.
     * Called once at BeginPlay. Entries not yet loaded on a flow tick are
     * skipped silently — they will be ready on a subsequent tick.
     */
    void RequestAsyncClassLoads();

    // ── Player Count ─────────────────────────────────────────────────────────
    int32 GetNearbyPlayerCount() const;

    // ── Guards ────────────────────────────────────────────────────────────────
    bool bDelegateWarningLogged = false;
};
```

---

## BeginPlay

```cpp
void USpawnManagerComponent::BeginPlay()
{
    Super::BeginPlay();

    // Server-only. No client path.
    if (!GetOwner() || !GetOwner()->HasAuthority())
        return;

    // Ensure SpawnPointConfig is valid.
    if (!SpawnPointConfig)
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("USpawnManagerComponent on [%s]: SpawnPointConfig is null. "
                 "Defaulting to USpawnPointConfig_RadiusRandom (Radius=500)."),
            *GetOwner()->GetName());
        SpawnPointConfig = NewObject<USpawnPointConfig_RadiusRandom>(this);
    }

    // Dev-build validation: warn about player-state requirements on spawn entries.
#if !UE_BUILD_SHIPPING
    for (const FSpawnEntry& Entry : SpawnEntries)
    {
        if (Entry.SpawnRequirements)
            URequirementLibrary::ValidateRequirements(
                Entry.SpawnRequirements, /*bRequireSync=*/true);
    }
#endif

    // Queue async loads for all soft class references.
    RequestAsyncClassLoads();

    // Start the flow timer.
    ScheduleNextFlowTick(0);
}
```

---

## EndPlay

```cpp
void USpawnManagerComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UWorld* World = GetWorld())
        World->GetTimerManager().ClearTimer(FlowTimerHandle);

    Super::EndPlay(Reason);
}
```

---

## Flow Timer

### OnFlowTimerExpired

```cpp
void USpawnManagerComponent::OnFlowTimerExpired()
{
    // 1. Sample nearby players (if configured).
    const int32 NearbyPlayers = bScaleByNearbyPlayers ? GetNearbyPlayerCount() : 0;

    // 2. Reschedule BEFORE spawn work. Spawn may be slow (async loads, nav queries).
    ScheduleNextFlowTick(NearbyPlayers);

    // 3. Process entries with a shared budget.
    int32 Budget = GlobalFlowCount;

    for (FSpawnEntry& Entry : SpawnEntries)
    {
        if (Budget <= 0) break;

        // Prune stale weak pointers and check vacancy.
        Entry.GetAndPruneLiveCount();
        if (Entry.GetVacancy() <= 0) continue;

        // Evaluate entry requirements (world-state only, no PlayerState).
        if (Entry.SpawnRequirements)
        {
            FRequirementContext Ctx;
            Ctx.World      = GetWorld();
            Ctx.Instigator = GetOwner();
            if (!Entry.SpawnRequirements->Evaluate(Ctx).bPassed)
                continue;
        }

        // Compute how many to spawn for this entry this tick.
        const int32 ToSpawn = FMath::Min(
            Entry.GetVacancy(),
            Entry.GetEffectiveBudget(Budget));

        for (int32 i = 0; i < ToSpawn; ++i)
        {
            AActor* Spawned = TrySpawnForEntry(Entry);
            if (Spawned)
                Budget--;
            // Failures are silent skips — no retry state, no budget deduction.
        }
    }
}
```

### ScheduleNextFlowTick

```cpp
void USpawnManagerComponent::ScheduleNextFlowTick(int32 NearbyPlayers)
{
    const float Interval = ComputeNextInterval(NearbyPlayers);
    GetWorld()->GetTimerManager().SetTimer(
        FlowTimerHandle,
        this,
        &USpawnManagerComponent::OnFlowTimerExpired,
        Interval,
        /*bLoop=*/false);
}
```

### ComputeNextInterval

```cpp
float USpawnManagerComponent::ComputeNextInterval(int32 NearbyPlayers) const
{
    float Interval = BaseFlowInterval;

    if (bScaleByNearbyPlayers && NearbyPlayers > 0)
    {
        const float Alpha = FMath::Clamp(
            static_cast<float>(NearbyPlayers) /
            static_cast<float>(PlayerCountForMinInterval),
            0.f, 1.f);
        Interval = FMath::Lerp(BaseFlowInterval, MinFlowInterval, Alpha);
    }

    // Hard floor — always enforced regardless of designer config.
    Interval = FMath::Max(Interval, 10.f);

    // Jitter: spreads simultaneous managers to avoid frame spikes.
    Interval += FMath::RandRange(0.f, 1.f);

    return Interval;
}
```

---

## TrySpawnForEntry

```cpp
AActor* USpawnManagerComponent::TrySpawnForEntry(FSpawnEntry& Entry)
{
    // Entity class must be loaded. Skip silently if not ready yet.
    if (Entry.EntityClass.IsNull()) return nullptr;
    UClass* LoadedClass = Entry.EntityClass.Get();
    if (!LoadedClass) return nullptr; // Async load pending.

    // Resolve a spawn transform.
    FTransform SpawnTransform;
    if (!SpawnPointConfig->ResolveSpawnTransform(GetOwner(), SpawnTransform))
        return nullptr; // No valid point — silent skip.

    // Spawn deferred so we can bind delegates before BeginPlay.
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
    Params.bDeferConstruction = true;

    AActor* Actor = GetWorld()->SpawnActor<AActor>(
        LoadedClass, SpawnTransform, Params);
    if (!Actor) return nullptr;

    // Bind destruction tracking before FinishSpawning.
    Actor->OnDestroyed.AddDynamic(
        this, &USpawnManagerComponent::OnSpawnedActorDestroyed);

    Actor->FinishSpawning(SpawnTransform);

    // Notify entity that it was spawned by this manager.
    if (Actor->Implements<USpawnableEntity>())
        ISpawnableEntity::Execute_OnSpawnedByManager(Actor, GetOwner());

    // Track the live instance.
    Entry.LiveInstances.Add(Actor);

    return Actor;
}
```

---

## OnSpawnedActorDestroyed

```cpp
void USpawnManagerComponent::OnSpawnedActorDestroyed(AActor* DestroyedActor)
{
    for (FSpawnEntry& Entry : SpawnEntries)
    {
        Entry.LiveInstances.RemoveAll(
            [DestroyedActor](const TWeakObjectPtr<AActor>& P)
            {
                return !P.IsValid() || P.Get() == DestroyedActor;
            });
    }
}
```

---

## GetNearbyPlayerCount

```cpp
int32 USpawnManagerComponent::GetNearbyPlayerCount() const
{
    if (OnCountNearbyPlayers)
        return OnCountNearbyPlayers(GetOwner()->GetActorLocation(), PlayerScanRadius);

    if (!bDelegateWarningLogged)
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("USpawnManagerComponent on [%s]: bScaleByNearbyPlayers is true but "
                 "OnCountNearbyPlayers is not bound. Interval scaling disabled. "
                 "Bind the delegate from the game module."),
            *GetOwner()->GetName());
        const_cast<USpawnManagerComponent*>(this)->bDelegateWarningLogged = true;
    }
    return 0;
}
```

---

## RequestAsyncClassLoads

```cpp
void USpawnManagerComponent::RequestAsyncClassLoads()
{
    TArray<FSoftObjectPath> Paths;
    for (const FSpawnEntry& Entry : SpawnEntries)
    {
        if (!Entry.EntityClass.IsNull() && !Entry.EntityClass.IsValid())
            Paths.Add(Entry.EntityClass.ToSoftObjectPath());
    }
    if (Paths.IsEmpty()) return;

    // UAssetManager / FStreamableManager handles deduplication.
    UAssetManager::GetStreamableManager().RequestAsyncLoad(
        Paths, FStreamableDelegate::CreateUObject(
            this, &USpawnManagerComponent::OnClassesLoaded));
}

void USpawnManagerComponent::OnClassesLoaded()
{
    // No action needed — class pointers are now resident.
    // The next flow tick will find them via EntityClass.Get().
    UE_LOG(LogGameCore, Verbose,
        TEXT("USpawnManagerComponent on [%s]: async class loads complete."),
        *GetOwner()->GetName());
}
```

Add `OnClassesLoaded` as a private member (non-UFUNCTION, used only as a streamable callback).

---

## GetLootTableOverrideForClass

```cpp
TSoftObjectPtr<ULootTable> USpawnManagerComponent::GetLootTableOverrideForClass(
    TSubclassOf<AActor> ActorClass) const
{
    for (const FSpawnEntry& Entry : SpawnEntries)
    {
        if (!Entry.EntityClass.IsNull() &&
            Entry.EntityClass.Get() == ActorClass)
        {
            return Entry.LootTableOverride;
        }
    }
    return nullptr;
}
```

---

## Full Flow Diagram

```
BeginPlay (server only)
  ├─ Validate SpawnPointConfig (create default if null)
  ├─ ValidateRequirements on all entries [dev builds only]
  ├─ RequestAsyncClassLoads()
  └─ ScheduleNextFlowTick(0)

OnFlowTimerExpired
  ├─ NearbyPlayers = GetNearbyPlayerCount() [if bScaleByNearbyPlayers]
  ├─ ScheduleNextFlowTick(NearbyPlayers)  ← FIRST
  ├─ Budget = GlobalFlowCount
  └─ For each FSpawnEntry:
       ├─ GetAndPruneLiveCount()                   [safety net prune]
       ├─ if Vacancy == 0: continue
       ├─ if SpawnRequirements fails: continue
       ├─ ToSpawn = Min(Vacancy, GetEffectiveBudget(Budget))
       └─ For i in [0, ToSpawn):
            └─ TrySpawnForEntry(Entry)
                 ├─ EntityClass loaded? no → skip
                 ├─ ResolveSpawnTransform → false → skip
                 ├─ SpawnActor (deferred)
                 ├─ Bind OnDestroyed
                 ├─ FinishSpawning
                 ├─ Execute_OnSpawnedByManager
                 ├─ LiveInstances.Add
                 └─ Budget--

OnSpawnedActorDestroyed(Actor)   [bound per-instance]
  └─ Scan all entries → remove matching weak pointer

ComputeNextInterval(NearbyPlayers)
  ├─ Interval = BaseFlowInterval
  ├─ if bScaleByNearbyPlayers:
  │    alpha = Clamp(NearbyPlayers / PlayerCountForMinInterval, 0, 1)
  │    Interval = Lerp(BaseFlowInterval, MinFlowInterval, alpha)
  ├─ Interval = Max(Interval, 10.0)   ← hard floor
  └─ Interval += RandRange(0, 1)      ← jitter
```

---

## Build.cs Dependencies

`GameCore.Build.cs` must include:

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "NavigationSystem",  // UNavigationSystemV1 for RadiusRandom projection
    "Engine",
    "GameplayTags",
    "Requirements",      // URequirementList, FRequirementContext
});
```

---

## Notes

- `bDeferConstruction = true` in spawn params is critical. It allows the `OnDestroyed` delegate to be bound before `BeginPlay` fires on the entity, guaranteeing tracking is set up even if the entity destroys itself during `BeginPlay`.
- `AdjustIfPossibleButDontSpawnIfColliding` is the correct collision handling mode. It allows minor positional adjustments (a few cm) to clear overlapping geometry while still failing hard if the spawn point is genuinely blocked. Do not use `AlwaysSpawn` — that would allow overlapping physics actors.
- `GlobalFlowCount` is intentionally a count of spawn *attempts*, not a strict per-tick guarantee. If `TrySpawnForEntry` fails (no navmesh, blocked), the budget is *not* decremented — the slot is a free retry next tick, not a wasted budget slot.
- The component does not emit any GameCore events on spawn or despawn. If downstream systems (AI, quest objectives) need to react, they should bind to the spawned actor's own lifecycle events, not to the spawn manager.
- In PIE, `HasAuthority` is true on the server world. The component will run normally in PIE listen-server setups.
