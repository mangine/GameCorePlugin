# UStatComponent

**Module:** `GameCore` | **File:** `Stats/StatComponent.h`

---

## Role

Actor component that lives on `APlayerState`. Owns runtime stat values for one player, auto-registers Event Bus listeners from authored `UStatIncrementRule` objects, evaluates requirement gates, persists values via `IPersistableComponent`, and broadcasts `FStatChangedEvent` on value change.

All stat mutation is server-authoritative. Methods that mutate state check `GetOwner()->HasAuthority()` and early-out on clients.

---

## Class Definition

```cpp
// GameCore/Source/GameCore/Stats/StatComponent.h

UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UStatComponent : public UActorComponent, public IPersistableComponent
{
    GENERATED_BODY()
public:
    // Authored in the PlayerState Blueprint. One entry per tracked stat.
    UPROPERTY(EditDefaultsOnly, Category="Stats")
    TArray<TObjectPtr<UStatDefinition>> Definitions;

    // Flush interval in seconds. Default: 10s.
    UPROPERTY(EditDefaultsOnly, Category="Stats", meta=(ClampMin="1.0"))
    float FlushIntervalSeconds = 10.f;

    // -------------------------------------------------------
    // Public API (server-authoritative)
    // -------------------------------------------------------

    // Add Delta to the stat identified by StatTag.
    // Requirements on the matching UStatDefinition are evaluated here.
    // Broadcasts FStatChangedEvent on the Event Bus if value changes.
    // No-ops silently on clients.
    UFUNCTION(BlueprintCallable, Category="Stats")
    void AddToStat(FGameplayTag StatTag, float Delta);

    // Returns current value. Safe to call from client (read-only).
    // Returns 0 on client unless game module replicates RuntimeValues.
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Stats")
    float GetStat(FGameplayTag StatTag) const;

    // -------------------------------------------------------
    // IPersistableComponent
    // -------------------------------------------------------
    virtual void Serialize_Implementation(FGameCoreArchive& Ar) override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    // Runtime values. Key = StatTag. Only modified on server.
    UPROPERTY()
    TMap<FGameplayTag, float> RuntimeValues;

    // Dirty set — tags modified since last flush.
    TSet<FGameplayTag> DirtyStats;

    // Flush timer handle.
    FTimerHandle FlushTimerHandle;

    // Handles for all auto-registered Event Bus listeners.
    // Populated in BeginPlay, cleared in EndPlay.
    TArray<FGameplayMessageListenerHandle> ListenerHandles;

    void RegisterListeners();
    void UnregisterListeners();
    void FlushDirtyStats();

    // Non-shipping: validates Definitions for nulls, duplicate tags, invalid channels.
    void ValidateDefinitions() const;
};
```

---

## BeginPlay

```cpp
void UStatComponent::BeginPlay()
{
    Super::BeginPlay();

#if !UE_BUILD_SHIPPING
    ValidateDefinitions();
#endif

    if (!GetOwner()->HasAuthority())
        return;

    RegisterListeners();

    GetWorld()->GetTimerManager().SetTimer(
        FlushTimerHandle,
        this,
        &UStatComponent::FlushDirtyStats,
        FlushIntervalSeconds,
        /*bLoop=*/true
    );
}
```

---

## RegisterListeners

Iterates every rule on every definition and registers one Event Bus listener per `(StatTag, ChannelTag)` pair. Multiple definitions sharing the same channel each get their own listener — no deduplication, same total call count either way.

```cpp
void UStatComponent::RegisterListeners()
{
    UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this);
    if (!Bus) return;

    for (UStatDefinition* Def : Definitions)
    {
        if (!Def) continue;

        for (UStatIncrementRule* Rule : Def->Rules)
        {
            if (!Rule) continue;

            const FGameplayTag Channel = Rule->GetChannelTag();
            if (!Channel.IsValid()) continue;

            // Def and Rule are DataAsset-owned UObjects with lifetimes exceeding
            // this component. Capturing raw pointers in the lambda is safe.
            FGameplayMessageListenerHandle Handle = Bus->StartListening(
                Channel,
                this,
                [this, Def, Rule](FGameplayTag /*Channel*/, const FInstancedStruct& Payload)
                {
                    const float Delta = Rule->ExtractIncrement(Payload);
                    if (Delta > 0.f)
                        AddToStat(Def->StatTag, Delta);
                }
            );

            ListenerHandles.Add(MoveTemp(Handle));
        }
    }
}
```

**Notes:**
- `AddToStat` performs its own requirements check — no need to gate inside the lambda.
- Invalid handles (empty channel, bus unavailable) are not added to `ListenerHandles`.

---

## UnregisterListeners

```cpp
void UStatComponent::UnregisterListeners()
{
    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        for (FGameplayMessageListenerHandle& Handle : ListenerHandles)
            Bus->StopListening(Handle);
    }
    ListenerHandles.Empty();
}
```

---

## EndPlay

```cpp
void UStatComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    UnregisterListeners();
    FlushDirtyStats();  // Force flush on disconnect — no stat loss.
    GetWorld()->GetTimerManager().ClearTimer(FlushTimerHandle);
    Super::EndPlay(Reason);
}
```

---

## AddToStat

```cpp
void UStatComponent::AddToStat(FGameplayTag StatTag, float Delta)
{
    if (!GetOwner()->HasAuthority()) return;
    if (Delta <= 0.f) return;

    // Evaluate requirements if a matching definition exists.
    for (const UStatDefinition* Def : Definitions)
    {
        if (Def && Def->StatTag == StatTag && Def->TrackingRequirements)
        {
            if (!Def->TrackingRequirements->AreRequirementsMet(GetOwner()))
                return;
            break;
        }
    }

    float& Value = RuntimeValues.FindOrAdd(StatTag, 0.f);
    Value += Delta;
    DirtyStats.Add(StatTag);

    FStatChangedEvent Event;
    Event.StatTag  = StatTag;
    Event.NewValue = Value;
    Event.Delta    = Delta;
    Event.PlayerId = GetOwner<APlayerState>()->GetUniqueId();

    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        Bus->Broadcast(
            TAG_Event_Stat_Changed,
            FInstancedStruct::Make(Event),
            EGameCoreEventScope::ServerOnly);
    }
}
```

> **Performance note:** The requirements loop is O(n) over `Definitions`. For projects with large definition arrays (> 50), consider building a `TMap<FGameplayTag, UStatDefinition*>` cache at `BeginPlay`.

---

## GetStat

```cpp
float UStatComponent::GetStat(FGameplayTag StatTag) const
{
    if (const float* Value = RuntimeValues.Find(StatTag))
        return *Value;
    return 0.f;
}
```

---

## Persistence

```cpp
void UStatComponent::Serialize_Implementation(FGameCoreArchive& Ar)
{
    Ar << RuntimeValues;
}

void UStatComponent::FlushDirtyStats()
{
    if (DirtyStats.Num() == 0) return;
    UPersistenceSubsystem::Get(this).RequestFlush(this);
    DirtyStats.Empty();
}
```

**Notes:**
- Persistence loads before `BeginPlay` — `RuntimeValues` is populated before any listener fires.
- `EndPlay` forces a flush to prevent stat loss on clean disconnect.
- Stats are per-character. Account-level stats require a separate account-scoped persistence record.

---

## ValidateDefinitions (non-shipping)

```cpp
void UStatComponent::ValidateDefinitions() const
{
    TSet<FGameplayTag> SeenTags;
    for (const UStatDefinition* Def : Definitions)
    {
        checkf(Def != nullptr,
            TEXT("UStatComponent: null UStatDefinition entry."));
        checkf(Def->StatTag.IsValid(),
            TEXT("UStatComponent: UStatDefinition has invalid StatTag."));
        checkf(!SeenTags.Contains(Def->StatTag),
            TEXT("UStatComponent: duplicate StatTag '%s'."), *Def->StatTag.ToString());
        SeenTags.Add(Def->StatTag);

        for (const UStatIncrementRule* Rule : Def->Rules)
        {
            checkf(Rule != nullptr,
                TEXT("UStatComponent: null Rule on definition '%s'."),
                *Def->StatTag.ToString());
            checkf(Rule->GetChannelTag().IsValid(),
                TEXT("UStatComponent: Rule on '%s' returns invalid ChannelTag."),
                *Def->StatTag.ToString());
        }
    }
}
```
