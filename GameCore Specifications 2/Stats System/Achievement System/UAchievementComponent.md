# UAchievementComponent

**Module:** `GameCore` | **File:** `Stats/Achievements/AchievementComponent.h`

---

## Role

Actor component on `APlayerState`. Owns the player's earned achievement set, evaluates achievements when relevant stats change, manages requirement watchers for `AdditionalRequirements`, and persists earned state and requirement payloads.

All mutation is server-authoritative. The client holds a replicated read-only earned set for UI.

---

## Class Definition

```cpp
// GameCore/Source/GameCore/Stats/Achievements/AchievementComponent.h

UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UAchievementComponent : public UActorComponent, public IPersistableComponent
{
    GENERATED_BODY()
public:
    // All achievements this player can earn. Authored on APlayerState Blueprint.
    UPROPERTY(EditDefaultsOnly, Category="Achievements")
    TArray<TObjectPtr<UAchievementDefinition>> Definitions;

    // -------------------------------------------------------
    // Public API
    // -------------------------------------------------------

    // Returns true if the player has earned this achievement.
    // Safe to call client-side (reads replicated EarnedAchievements).
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Achievements")
    bool HasAchievement(FGameplayTag AchievementTag) const;

    // Fills OutProgress with current/required values for each stat threshold.
    // Returns false if AchievementTag is unknown or already earned.
    // Reads live from UStatComponent — never cached separately.
    // NOTE: Returns 0 on client unless game module replicates stat values.
    UFUNCTION(BlueprintCallable, Category="Achievements")
    bool GetProgress(FGameplayTag AchievementTag, TArray<FStatThresholdProgress>& OutProgress) const;

    // Server only. Called by game module bridge to supply URequirement_Persisted
    // data for achievements whose AdditionalRequirements reference persisted requirements.
    // Replaces any existing payload for this AchievementTag.
    // Triggers an immediate watcher flush for the affected requirement set.
    UFUNCTION(BlueprintCallable, Category="Achievements", BlueprintAuthorityOnly)
    void InjectRequirementPayload(FGameplayTag AchievementTag, const FRequirementPayload& Payload);

    // Delegate broadcast on client when EarnedAchievements is updated (via OnRep).
    UPROPERTY(BlueprintAssignable, Category="Achievements")
    FSimpleDynamicMulticastDelegate OnAchievementsChanged;

    // -------------------------------------------------------
    // IPersistableComponent
    // -------------------------------------------------------
    virtual void Serialize_Implementation(FGameCoreArchive& Ar) override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    // -------------------------------------------------------
    // Persisted state
    // -------------------------------------------------------

    // Replicated to owning client for UI. Tags are added, never removed.
    UPROPERTY(ReplicatedUsing=OnRep_EarnedAchievements)
    FGameplayTagContainer EarnedAchievements;

    // Persisted. Only populated for achievements using URequirement_Persisted.
    // Entries are removed on grant, so this stays sparse over time.
    UPROPERTY()
    TMap<FGameplayTag, FRequirementPayload> RequirementPayloads;

    // -------------------------------------------------------
    // Runtime state (not persisted)
    // -------------------------------------------------------

    // Key = StatTag. Built at BeginPlay. Values are non-owning pointers into
    // Definitions — lifetime guaranteed by UPROPERTY above.
    TMap<FGameplayTag, TArray<TObjectPtr<UAchievementDefinition>>> StatToAchievements;

    // Key = AchievementTag. Only entries exist for achievements with AdditionalRequirements.
    TMap<FGameplayTag, FRequirementSetHandle> WatcherHandles;

    // Event Bus listener handle for FStatChangedEvent.
    FGameplayMessageListenerHandle StatChangedHandle;

    // -------------------------------------------------------
    // Internal
    // -------------------------------------------------------
    void BuildLookupMap();
    void RegisterWatchers();
    void UnregisterWatcher(FGameplayTag AchievementTag);

    void OnStatChanged(const FStatChangedEvent& Event);
    void EvaluateAchievement(const UAchievementDefinition* Def);
    bool CheckStatThresholds(const UAchievementDefinition* Def) const;
    void GrantAchievement(FGameplayTag AchievementTag);

    void OnWatcherDirty(FRequirementSetHandle Handle, bool bAllPassed, FGameplayTag AchievementTag);

    UFUNCTION()
    void OnRep_EarnedAchievements();

#if !UE_BUILD_SHIPPING
    void ValidateDefinitions() const;
#endif
};
```

---

## BeginPlay

```cpp
void UAchievementComponent::BeginPlay()
{
    Super::BeginPlay();

#if !UE_BUILD_SHIPPING
    ValidateDefinitions();
#endif

    if (!GetOwner()->HasAuthority()) return;

    BuildLookupMap();
    RegisterWatchers();

    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        StatChangedHandle = Bus->StartListening<FStatChangedEvent>(
            TAG_GameCoreEvent_Stat_Changed, this,
            [this](FGameplayTag, const FStatChangedEvent& Event)
            {
                OnStatChanged(Event);
            });
    }
}
```

---

## BuildLookupMap

Builds the `StatTag → achievements` map from `Definitions`. The component's `Definitions` array is the authoritative source; `UStatDefinition.AffectedAchievements` is for content-authoring guidance only.

```cpp
void UAchievementComponent::BuildLookupMap()
{
    StatToAchievements.Reset();

    for (UAchievementDefinition* Def : Definitions)
    {
        if (!ensureAlwaysMsgf(Def,
            TEXT("Null UAchievementDefinition in UAchievementComponent.Definitions")))
            continue;
        if (!ensureAlwaysMsgf(Def->StatThresholds.Num() > 0,
            TEXT("UAchievementDefinition '%s' has no StatThresholds"),
            *Def->AchievementTag.ToString()))
            continue;

        for (const FStatThreshold& Threshold : Def->StatThresholds)
            StatToAchievements.FindOrAdd(Threshold.StatTag).AddUnique(Def);
    }
}
```

---

## RegisterWatchers

Registers `URequirementWatcherComponent` callbacks only for achievements with `AdditionalRequirements` that are not yet earned.

```cpp
void UAchievementComponent::RegisterWatchers()
{
    URequirementWatcherComponent* Watcher =
        GetOwner()->FindComponentByClass<URequirementWatcherComponent>();
    if (!Watcher) return;

    for (UAchievementDefinition* Def : Definitions)
    {
        if (!Def->AdditionalRequirements) continue;
        if (EarnedAchievements.HasTag(Def->AchievementTag)) continue; // Already earned.

        FGameplayTag AchTag = Def->AchievementTag;
        TWeakObjectPtr<UAchievementComponent> WeakThis = this;

        FRequirementSetHandle Handle = Watcher->RegisterSet(
            Def->AdditionalRequirements,
            FOnRequirementSetDirty::CreateLambda(
                [WeakThis, AchTag](FRequirementSetHandle H, bool bAllPassed)
                {
                    if (UAchievementComponent* AC = WeakThis.Get())
                        AC->OnWatcherDirty(H, bAllPassed, AchTag);
                }),
            // ContextBuilder: injects persisted payload for URequirement_Persisted subclasses.
            [WeakThis, AchTag](FRequirementContext& Ctx)
            {
                if (UAchievementComponent* AC = WeakThis.Get())
                {
                    if (const FRequirementPayload* Payload = AC->RequirementPayloads.Find(AchTag))
                        Ctx.PersistedData.Add(AchTag, *Payload);
                }
            }
        );

        WatcherHandles.Add(AchTag, Handle);
    }
}
```

---

## Evaluation — Stat-Triggered Path

```cpp
void UAchievementComponent::OnStatChanged(const FStatChangedEvent& Event)
{
    const TArray<TObjectPtr<UAchievementDefinition>>* Affected =
        StatToAchievements.Find(Event.StatTag);
    if (!Affected) return;

    for (const TObjectPtr<UAchievementDefinition>& Def : *Affected)
        EvaluateAchievement(Def);
}

void UAchievementComponent::EvaluateAchievement(const UAchievementDefinition* Def)
{
    if (!Def || EarnedAchievements.HasTag(Def->AchievementTag)) return;
    if (!CheckStatThresholds(Def)) return;

    if (!Def->AdditionalRequirements)
    {
        GrantAchievement(Def->AchievementTag);
        return;
    }

    // Requirements watcher owns the result. If it last resolved true, grant now.
    // Otherwise the watcher will call OnWatcherDirty when it passes.
    URequirementWatcherComponent* Watcher =
        GetOwner()->FindComponentByClass<URequirementWatcherComponent>();
    if (!Watcher) return;

    const FRequirementSetHandle* Handle = WatcherHandles.Find(Def->AchievementTag);
    if (Handle && Watcher->GetLastResult(*Handle))
        GrantAchievement(Def->AchievementTag);
}
```

## Evaluation — Watcher-Triggered Path

```cpp
void UAchievementComponent::OnWatcherDirty(
    FRequirementSetHandle Handle, bool bAllPassed, FGameplayTag AchievementTag)
{
    if (!bAllPassed) return;
    if (EarnedAchievements.HasTag(AchievementTag)) return;

    const UAchievementDefinition* Def = Definitions.FindByPredicate(
        [&](const TObjectPtr<UAchievementDefinition>& D)
        { return D && D->AchievementTag == AchievementTag; });

    if (Def && CheckStatThresholds(Def))
        GrantAchievement(AchievementTag);
}
```

---

## CheckStatThresholds

```cpp
bool UAchievementComponent::CheckStatThresholds(const UAchievementDefinition* Def) const
{
    const UStatComponent* Stats = GetOwner()->FindComponentByClass<UStatComponent>();
    if (!Stats) return false;

    for (const FStatThreshold& T : Def->StatThresholds)
    {
        if (Stats->GetStat(T.StatTag) < T.Threshold)
            return false;
    }
    return true;
}
```

---

## GrantAchievement

```cpp
void UAchievementComponent::GrantAchievement(FGameplayTag AchievementTag)
{
    check(GetOwner()->HasAuthority());
    check(!EarnedAchievements.HasTag(AchievementTag));

    EarnedAchievements.AddTag(AchievementTag);
    MarkDirty(); // IPersistableComponent dirty flag

    // Watcher is no longer needed — achievement is monotonic.
    UnregisterWatcher(AchievementTag);
    // Remove persisted payload — no longer needed after grant.
    RequirementPayloads.Remove(AchievementTag);

    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        FAchievementUnlockedEvent Evt;
        Evt.AchievementTag = AchievementTag;
        Evt.PlayerId       = GetOwner<APlayerState>()->GetUniqueId();
        Bus->Broadcast(
            TAG_GameCoreEvent_Achievement_Unlocked,
            FInstancedStruct::Make(Evt),
            EGameCoreEventScope::ServerOnly);
    }
}
```

---

## InjectRequirementPayload

```cpp
void UAchievementComponent::InjectRequirementPayload(
    FGameplayTag AchievementTag, const FRequirementPayload& Payload)
{
    if (!GetOwner()->HasAuthority()) return;
    if (EarnedAchievements.HasTag(AchievementTag)) return; // Already earned — discard.

    RequirementPayloads.FindOrAdd(AchievementTag) = Payload;
    MarkDirty();

    if (URequirementWatcherComponent* Watcher =
        GetOwner()->FindComponentByClass<URequirementWatcherComponent>())
    {
        if (FRequirementSetHandle* Handle = WatcherHandles.Find(AchievementTag))
            Watcher->ForceFlush(*Handle);
    }
}
```

---

## GetProgress

```cpp
bool UAchievementComponent::GetProgress(
    FGameplayTag AchievementTag, TArray<FStatThresholdProgress>& OutProgress) const
{
    if (EarnedAchievements.HasTag(AchievementTag)) return false;

    const UAchievementDefinition* Def = Definitions.FindByPredicate(
        [&](const TObjectPtr<UAchievementDefinition>& D)
        { return D && D->AchievementTag == AchievementTag; });
    if (!Def) return false;

    const UStatComponent* Stats = GetOwner()->FindComponentByClass<UStatComponent>();

    OutProgress.Reset(Def->StatThresholds.Num());
    for (const FStatThreshold& T : Def->StatThresholds)
    {
        FStatThresholdProgress& P = OutProgress.AddDefaulted_GetRef();
        P.StatTag  = T.StatTag;
        P.Current  = Stats ? Stats->GetStat(T.StatTag) : 0.f;
        P.Required = T.Threshold;
    }
    return true;
}
```

---

## Persistence

```cpp
void UAchievementComponent::Serialize_Implementation(FGameCoreArchive& Ar)
{
    Ar << EarnedAchievements;
    Ar << RequirementPayloads; // Sparse — entries removed on grant.
}
```

**Notes:**
- Persistence loads before `BeginPlay` fires, so `RegisterWatchers` correctly skips already-earned achievements.
- Stat progress is not persisted here — it lives in `UStatComponent`.
- `RequirementPayloads` stays sparse: entries are removed on grant.

---

## Replication

```cpp
void UAchievementComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    // Only replicate to the owning client — other players' achievements are private.
    DOREPLIFETIME_CONDITION(UAchievementComponent, EarnedAchievements, COND_OwnerOnly);
}

void UAchievementComponent::OnRep_EarnedAchievements()
{
    // Notify UI. Broadcast a local delegate — UI widgets bind to this.
    OnAchievementsChanged.Broadcast();
}
```

`COND_OwnerOnly` — other players' achievement state is not replicated. Public achievement display (e.g. player inspect) requires a separate opt-in replication path in the game module.

---

## EndPlay

```cpp
void UAchievementComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
        Bus->StopListening(StatChangedHandle);

    // Watchers are owned by URequirementWatcherComponent — it handles its own cleanup.
    WatcherHandles.Empty();

    Super::EndPlay(Reason);
}
```

---

## Non-Shipping Validation

```cpp
#if !UE_BUILD_SHIPPING
void UAchievementComponent::ValidateDefinitions() const
{
    TSet<FGameplayTag> SeenTags;
    for (const UAchievementDefinition* Def : Definitions)
    {
        if (!Def)
        {
            UE_LOG(LogGameCore, Error,
                TEXT("UAchievementComponent: null entry in Definitions."));
            continue;
        }
        if (SeenTags.Contains(Def->AchievementTag))
            UE_LOG(LogGameCore, Error,
                TEXT("UAchievementComponent: duplicate AchievementTag '%s'."),
                *Def->AchievementTag.ToString());
        SeenTags.Add(Def->AchievementTag);

        if (Def->StatThresholds.IsEmpty())
            UE_LOG(LogGameCore, Error,
                TEXT("UAchievementComponent: achievement '%s' has no StatThresholds."),
                *Def->AchievementTag.ToString());
    }
}
#endif
```
