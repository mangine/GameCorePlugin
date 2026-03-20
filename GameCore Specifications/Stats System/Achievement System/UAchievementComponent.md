# UAchievementComponent

**Sub-page of:** [Achievement System](./Achievement%20System.md)

---

## Role

Actor component on `APlayerState`. Owns the player's earned achievement set, evaluates achievements when relevant stats change, manages requirement watchers for `AdditionalRequirements`, and persists earned state and requirement payloads.

All mutation is server-authoritative. The client holds a replicated read-only earned set for UI.

---

## File Location

```
GameCore/Source/GameCore/Stats/
├── AchievementComponent.h
├── AchievementComponent.cpp
└── AchievementTypes.h   // FStatThreshold, FStatThresholdProgress, FAchievementUnlockedEvent
```

---

## Class Definition

```cpp
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UAchievementComponent : public UActorComponent, public IPersistableComponent
{
    GENERATED_BODY()
public:
    // All achievements this player can earn. Authored on APlayerState Blueprint.
    // Must match the union of all AffectedAchievements entries across all UStatDefinition assets.
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
    UFUNCTION(BlueprintCallable, Category="Achievements")
    bool GetProgress(FGameplayTag AchievementTag, TArray<FStatThresholdProgress>& OutProgress) const;

    // Server only. Called by the game module bridge to supply URequirement_Persisted data
    // for achievements whose AdditionalRequirements reference persisted requirements.
    // Replaces any existing payload for this AchievementTag.
    // Triggers an immediate watcher flush for the affected achievement's requirement set.
    UFUNCTION(BlueprintCallable, Category="Achievements", BlueprintAuthorityOnly)
    void InjectRequirementPayload(FGameplayTag AchievementTag, const FRequirementPayload& Payload);

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

    // Replicated to owning client for UI. Tags are added; never removed.
    UPROPERTY(ReplicatedUsing=OnRep_EarnedAchievements)
    FGameplayTagContainer EarnedAchievements;

    // Persisted. Only populated for achievements whose AdditionalRequirements
    // reference URequirement_Persisted subclasses. Typically sparse.
    // Key = AchievementTag.
    UPROPERTY()
    TMap<FGameplayTag, FRequirementPayload> RequirementPayloads;

    // -------------------------------------------------------
    // Runtime state (not persisted)
    // -------------------------------------------------------

    // Key = StatTag. Built at BeginPlay from Definitions + UStatDefinition.AffectedAchievements.
    // Values are non-owning pointers into Definitions — lifetime guaranteed by UPROPERTY above.
    TMap<FGameplayTag, TArray<TObjectPtr<UAchievementDefinition>>> StatToAchievements;

    // Key = AchievementTag. Only entries exist for achievements with AdditionalRequirements.
    TMap<FGameplayTag, FRequirementSetHandle> WatcherHandles;

    // EventBus2 listener for FStatChangedEvent.
    FGameplayMessageListenerHandle StatChangedHandle;

    // -------------------------------------------------------
    // Internal
    // -------------------------------------------------------
    void BuildLookupMap();
    void RegisterWatchers();
    void UnregisterWatcher(FGameplayTag AchievementTag);

    void OnStatChanged(FGameplayTag Channel, const FStatChangedEvent& Event);
    void EvaluateAchievement(const UAchievementDefinition* Def);
    bool CheckStatThresholds(const UAchievementDefinition* Def) const;
    void GrantAchievement(FGameplayTag AchievementTag);

    void OnWatcherDirty(FRequirementSetHandle Handle, bool bAllPassed, FGameplayTag AchievementTag);

    UFUNCTION()
    void OnRep_EarnedAchievements();
};
```

---

## BeginPlay

```cpp
void UAchievementComponent::BeginPlay()
{
    Super::BeginPlay();
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

Resolves soft refs on `UStatDefinition.AffectedAchievements` and builds the `StatTag → achievements` map.

```cpp
void UAchievementComponent::BuildLookupMap()
{
    StatToAchievements.Reset();

    // For each achievement this component owns:
    for (UAchievementDefinition* Def : Definitions)
    {
        if (!ensureAlwaysMsgf(Def, TEXT("Null UAchievementDefinition in UAchievementComponent.Definitions")))
            continue;
        if (!ensureAlwaysMsgf(Def->StatThresholds.Num() > 0,
            TEXT("UAchievementDefinition '%s' has no StatThresholds"), *Def->AchievementTag.ToString()))
            continue;

        for (const FStatThreshold& Threshold : Def->StatThresholds)
        {
            StatToAchievements.FindOrAdd(Threshold.StatTag).AddUnique(Def);
        }
    }
}
```

**Note:** The map is built from `Definitions` (the authoritative component array), not by crawling `UStatDefinition` assets. This means the content author is responsible for keeping `UStatDefinition.AffectedAchievements` and `UAchievementComponent.Definitions` in sync. A non-shipping validation pass in `BeginPlay` warns on mismatches.

---

## RegisterWatchers

```cpp
void UAchievementComponent::RegisterWatchers()
{
    URequirementWatcherComponent* Watcher =
        GetOwner()->FindComponentByClass<URequirementWatcherComponent>();
    if (!Watcher) return;

    for (UAchievementDefinition* Def : Definitions)
    {
        if (!Def->AdditionalRequirements || EarnedAchievements.HasTag(Def->AchievementTag))
            continue; // Already earned — skip watcher registration entirely.

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

## Evaluation Flow

### Stat-Triggered Path

```
FStatChangedEvent fires on GameCoreEvent.Stat.Changed
  → UAchievementComponent::OnStatChanged
  → lookup StatToAchievements[Event.StatTag]
  → for each Def in results:
      → skip if EarnedAchievements.HasTag(Def->AchievementTag)
      → CheckStatThresholds(Def)
          → for each FStatThreshold: GetStat(StatTag) >= Threshold
      → if all thresholds pass:
          → if Def->AdditionalRequirements == null → GrantAchievement()
          → else: check watcher's last cached result
              → if watcher last resolved true → GrantAchievement()
              → else: wait for watcher (it will trigger OnWatcherDirty)
```

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

    // Watcher owns the requirement evaluation result.
    // If the watcher already resolved true on its last flush, grant now.
    // Otherwise the watcher will call OnWatcherDirty when it passes.
    URequirementWatcherComponent* Watcher =
        GetOwner()->FindComponentByClass<URequirementWatcherComponent>();
    if (!Watcher) return;

    const FRequirementSetHandle* Handle = WatcherHandles.Find(Def->AchievementTag);
    if (Handle && Watcher->GetLastResult(*Handle))
        GrantAchievement(Def->AchievementTag);
}
```

### Watcher-Triggered Path

```cpp
void UAchievementComponent::OnWatcherDirty(
    FRequirementSetHandle Handle, bool bAllPassed, FGameplayTag AchievementTag)
{
    if (!bAllPassed) return;
    if (EarnedAchievements.HasTag(AchievementTag)) return;

    // Find the definition and re-check stat thresholds.
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

    // Unregister watcher — achievement is monotonic, watching is now waste.
    UnregisterWatcher(AchievementTag);
    // Remove persisted payload — no longer needed.
    RequirementPayloads.Remove(AchievementTag);

    // Broadcast for downstream consumers (rewards, UI, audio).
    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        FAchievementUnlockedEvent Evt;
        Evt.AchievementTag = AchievementTag;
        Evt.PlayerId = GetOwnerPlayerState()->GetUniqueId();
        Bus->Broadcast(TAG_GameCoreEvent_Achievement_Unlocked, Evt, EGameCoreEventScope::Server);
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

    // Force watcher re-evaluation now that new data is available.
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
    Ar << RequirementPayloads; // Sparse — usually empty or very small.
}
```

**Notes:**
- Persistence loads before `BeginPlay` fires, so `RegisterWatchers` correctly skips already-earned achievements.
- Stat progress is not persisted here — it lives in `UStatComponent`.
- `RequirementPayloads` entries are removed on grant, so the persisted map stays small over time.

---

## Replication

```cpp
void UAchievementComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UAchievementComponent, EarnedAchievements, COND_OwnerOnly);
}

void UAchievementComponent::OnRep_EarnedAchievements()
{
    // Client-side delegate for UI refresh. Broadcast a local event or call UI delegate.
    OnAchievementsChanged.Broadcast();
}
```

`COND_OwnerOnly` — other players' achievement state is not replicated. If a game needs public achievement display (e.g. player inspect), it should use a separate opt-in replication path.

---

## Non-Shipping Validation

At `BeginPlay` (non-shipping builds only):

```cpp
#if !UE_BUILD_SHIPPING
void UAchievementComponent::ValidateDefinitions() const
{
    TSet<FGameplayTag> SeenTags;
    for (const UAchievementDefinition* Def : Definitions)
    {
        if (!Def) { UE_LOG(LogGameCore, Error, TEXT("Null UAchievementDefinition in UAchievementComponent")); continue; }
        if (SeenTags.Contains(Def->AchievementTag))
            UE_LOG(LogGameCore, Error, TEXT("Duplicate AchievementTag: %s"), *Def->AchievementTag.ToString());
        SeenTags.Add(Def->AchievementTag);
        if (Def->StatThresholds.IsEmpty())
            UE_LOG(LogGameCore, Error, TEXT("Achievement '%s' has no StatThresholds"), *Def->AchievementTag.ToString());
    }
}
#endif
```
