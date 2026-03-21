# USpawnManagerComponent

**Module:** `GameCore`  
**File:** `GameCore/Source/GameCore/Spawning/SpawnManagerComponent.h / .cpp`

Server-only `UActorComponent`. Placed on a spawn anchor actor. Drives the full spawn lifecycle: async class loading, flow-timer scheduling, per-entry requirement evaluation, spawn-point resolution, live-instance tracking, and player-count-based interval scaling.

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
     * Base seconds between flow ticks. Always >= 10 s at runtime.
     * Actual interval = ComputeNextInterval(NearbyPlayers).
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
     * Must not be null at BeginPlay — if null, a USpawnPointConfig_RadiusRandom
     * (Radius=500) is created automatically and a Warning is logged.
     */
    UPROPERTY(EditAnywhere, Instanced, Category = "Spawning|SpawnPoint")
    TObjectPtr<USpawnPointConfig> SpawnPointConfig;

    // ── Player Multiplier ─────────────────────────────────────────────────────

    /**
     * If true, the flow interval is scaled down based on nearby player count.
     * Sampled once per flow tick. Requires OnCountNearbyPlayers to be bound.
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
     * Minimum achievable flow interval (seconds) at max player density.
     * Always >= 10 s at runtime regardless of designer config.
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
     * scanning SpawnEntries for a matching loaded EntityClass.
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
     * Returns the spawned actor on success, nullptr on failure (silent skip).
     * On success: binds OnDestroyed, calls OnSpawnedByManager, adds to LiveInstances.
     */
    AActor* TrySpawnForEntry(FSpawnEntry& Entry);

    /**
     * Bound to every spawned actor's OnDestroyed delegate.
     * Removes the actor from all entries' LiveInstances immediately.
     */
    UFUNCTION()
    void OnSpawnedActorDestroyed(AActor* DestroyedActor);

    // ── Async Class Loading ───────────────────────────────────────────────────
    /**
     * Queues async load for any entries whose EntityClass is not yet loaded.
     * Called once at BeginPlay.
     */
    void RequestAsyncClassLoads();
    void OnClassesLoaded(); // FStreamableDelegate callback — not a UFUNCTION.

    // ── Player Count ─────────────────────────────────────────────────────────
    int32 GetNearbyPlayerCount() const;

    // ── Guards ────────────────────────────────────────────────────────────────
    mutable bool bDelegateWarningLogged = false;
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

    // Dev-build validation: catch player-state requirements misused on spawn entries.
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
    // 1. Sample nearby players.
    const int32 NearbyPlayers = bScaleByNearbyPlayers ? GetNearbyPlayerCount() : 0;

    // 2. Reschedule BEFORE spawn work — work duration must not skew the interval.
    ScheduleNextFlowTick(NearbyPlayers);

    // 3. Process entries with a shared budget.
    int32 Budget = GlobalFlowCount;

    for (FSpawnEntry& Entry : SpawnEntries)
    {
        if (Budget <= 0) break;

        Entry.GetAndPruneLiveCount(); // Safety net prune.
        if (Entry.GetVacancy() <= 0) continue;

        // Evaluate world-state requirements (PlayerState = null).
        if (Entry.SpawnRequirements)
        {
            FRequirementContext Ctx;
            Ctx.World      = GetWorld();
            Ctx.Instigator = GetOwner();
            if (!Entry.SpawnRequirements->Evaluate(Ctx).bPassed)
                continue;
        }

        const int32 ToSpawn = FMath::Min(
            Entry.GetVacancy(),
            Entry.GetEffectiveBudget(Budget));

        for (int32 i = 0; i < ToSpawn; ++i)
        {
            AActor* Spawned = TrySpawnForEntry(Entry);
            if (Spawned)
                Budget--;
            // Spawn failures do NOT consume budget — free retry next tick.
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

    Interval = FMath::Max(Interval, 10.f); // Hard floor — always enforced.
    Interval += FMath::RandRange(0.f, 1.f); // Jitter.
    return Interval;
}
```

---

## TrySpawnForEntry

```cpp
AActor* USpawnManagerComponent::TrySpawnForEntry(FSpawnEntry& Entry)
{
    // Class must be loaded. Skip silently if async load is still pending.
    if (Entry.EntityClass.IsNull()) return nullptr;
    UClass* LoadedClass = Entry.EntityClass.Get();
    if (!LoadedClass) return nullptr;

    // Resolve a spawn transform.
    FTransform SpawnTransform;
    if (!SpawnPointConfig->ResolveSpawnTransform(GetOwner(), SpawnTransform))
        return nullptr; // Silent skip — retry next natural tick.

    // Spawn deferred so OnDestroyed can be bound before BeginPlay fires on
    // the entity. This guarantees tracking even if the entity self-destroys
    // during its own BeginPlay.
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
    Params.bDeferConstruction = true;

    AActor* Actor = GetWorld()->SpawnActor<AActor>(LoadedClass, SpawnTransform, Params);
    if (!Actor) return nullptr;

    // Bind destruction tracking before FinishSpawning.
    Actor->OnDestroyed.AddDynamic(this, &USpawnManagerComponent::OnSpawnedActorDestroyed);

    Actor->FinishSpawning(SpawnTransform);

    // Notify entity of spawn context.
    if (Actor->Implements<USpawnableEntity>())
        ISpawnableEntity::Execute_OnSpawnedByManager(Actor, GetOwner());

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
                 "OnCountNearbyPlayers is not bound. Interval scaling disabled."),
            *GetOwner()->GetName());
        bDelegateWarningLogged = true; // mutable — no const_cast needed.
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

    UAssetManager::GetStreamableManager().RequestAsyncLoad(
        Paths, FStreamableDelegate::CreateUObject(
            this, &USpawnManagerComponent::OnClassesLoaded));
}

void USpawnManagerComponent::OnClassesLoaded()
{
    // Class pointers are now resident. The next natural flow tick will find
    // them via EntityClass.Get() — no explicit action needed here.
    UE_LOG(LogGameCore, Verbose,
        TEXT("USpawnManagerComponent on [%s]: async class loads complete."),
        *GetOwner()->GetName());
}
```

---

## GetLootTableOverrideForClass

```cpp
TSoftObjectPtr<ULootTable> USpawnManagerComponent::GetLootTableOverrideForClass(
    TSubclassOf<AActor> ActorClass) const
{
    if (!ActorClass) return nullptr;
    for (const FSpawnEntry& Entry : SpawnEntries)
    {
        // Compare against the loaded class directly.
        // EntityClass.Get() returns null if the class hasn't loaded yet,
        // which is safe — GetLootTableOverrideForClass is only ever called
        // from OnSpawnedByManager, which fires post-spawn (class already loaded).
        if (Entry.EntityClass.Get() == ActorClass.Get())
            return Entry.LootTableOverride;
    }
    return nullptr;
}
```

---

## Notes

- `bDeferConstruction = true` in spawn params is critical. It allows `OnDestroyed` to be bound before the entity's `BeginPlay` fires, guaranteeing tracking even if the entity destroys itself during initialisation.
- `AdjustIfPossibleButDontSpawnIfColliding` is the correct collision handling mode. It permits minor positional adjustments while still failing hard if the spawn point is genuinely blocked. Do not use `AlwaysSpawn` — that can overlap physics actors.
- Spawn failures do **not** consume global budget. A failed slot is a free retry next tick — not a wasted budget slot.
- The component emits **no GameCore events** on spawn or despawn. Downstream systems (AI, quest objectives) should bind to the spawned actor's own lifecycle events, not to the spawn manager.
- In PIE, `HasAuthority()` is true on the server world. The component runs normally in listen-server PIE setups.
- `bDelegateWarningLogged` is declared `mutable` so `GetNearbyPlayerCount()` can set it without a `const_cast`.
