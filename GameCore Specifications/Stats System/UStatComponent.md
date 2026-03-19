# UStatComponent

## Role

Actor component that lives on `APlayerState`. Owns runtime stat values for one player, auto-registers GMS listeners from `UStatDefinition` assets, and persists values via `IPersistableComponent`.

All stat mutation is server-authoritative. Methods that mutate state check `GetOwner()->HasAuthority()` and early-out on clients.

---

## Class Definition

```cpp
// GameCore module
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UStatComponent : public UActorComponent, public IPersistableComponent
{
    GENERATED_BODY()

public:
    // Authored in the PlayerState Blueprint. One entry per tracked stat.
    UPROPERTY(EditDefaultsOnly, Category="Stats")
    TArray<TObjectPtr<UStatDefinition>> Definitions;

    // -------------------------------------------------------
    // Public API (server-authoritative)
    // -------------------------------------------------------

    // Add Delta to the stat identified by StatTag.
    // Requirements on the matching UStatDefinition are evaluated here.
    // Broadcasts FStatChangedEvent on the Event Bus if value changes.
    // Safe to call from anywhere server-side; no-ops silently on clients.
    UFUNCTION(BlueprintCallable, Category="Stats")
    void AddToStat(FGameplayTag StatTag, float Delta);

    // Returns current value. Safe to call from client (read-only).
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

    // GMS listener handles, stored to unregister on EndPlay.
    TArray<FGameplayMessageListenerHandle> ListenerHandles;

    // Flush timer handle.
    FTimerHandle FlushTimerHandle;

    void RegisterGMSListeners();
    void UnregisterGMSListeners();
    void OnMessageReceived(FGameplayTag ChannelTag, const FInstancedStruct& Payload,
                           UStatIncrementRule* Rule, UStatDefinition* Definition);
    void FlushDirtyStats();

    // Non-shipping validation: duplicate StatTags, null Rules, etc.
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
        return; // listeners registered server-side only

    RegisterGMSListeners();

    // Flush dirty stats every 10 seconds (configurable).
    GetWorld()->GetTimerManager().SetTimer(
        FlushTimerHandle,
        this,
        &UStatComponent::FlushDirtyStats,
        10.f,
        true
    );
}
```

---

## RegisterGMSListeners

For each Definition, for each Rule — register one untyped GMS listener. Multiple rules on the same channel produce independent listeners; the component routes each to the correct rule.

```cpp
void UStatComponent::RegisterGMSListeners()
{
    UGameplayMessageSubsystem& GMS = UGameplayMessageSubsystem::Get(this);

    for (UStatDefinition* Def : Definitions)
    {
        if (!Def) continue;

        for (UStatIncrementRule* Rule : Def->Rules)
        {
            if (!Rule) continue;

            FGameplayTag Channel = Rule->GetChannelTag();
            if (!Channel.IsValid()) continue;

            // Capture Rule and Def by pointer — both are UObjects, safe as long as component is alive.
            FGameplayMessageListenerHandle Handle = GMS.RegisterListener<FInstancedStruct>(
                Channel,
                [this, Rule, Def](FGameplayTag Tag, const FInstancedStruct& Payload)
                {
                    OnMessageReceived(Tag, Payload, Rule, Def);
                }
            );

            ListenerHandles.Add(MoveTemp(Handle));
        }
    }
}
```

> **Note:** GMS supports untyped / `FInstancedStruct` channel registration. If your GMS integration uses strongly-typed channels, wrap the listener registration in a thin adapter or broadcast `FInstancedStruct` wrappers from your message senders. See Integration page.

---

## OnMessageReceived

```cpp
void UStatComponent::OnMessageReceived(
    FGameplayTag ChannelTag,
    const FInstancedStruct& Payload,
    UStatIncrementRule* Rule,
    UStatDefinition* Definition)
{
    if (!GetOwner()->HasAuthority()) return;

    // Evaluate requirements if set.
    if (Definition->TrackingRequirements)
    {
        if (!Definition->TrackingRequirements->AreRequirementsMet(GetOwner()))
            return;
    }

    const float Delta = Rule->ExtractIncrement(Payload);
    if (Delta <= 0.f) return;

    AddToStat(Definition->StatTag, Delta);
}
```

---

## AddToStat

```cpp
void UStatComponent::AddToStat(FGameplayTag StatTag, float Delta)
{
    if (!GetOwner()->HasAuthority()) return;
    if (Delta <= 0.f) return;

    float& Value = RuntimeValues.FindOrAdd(StatTag, 0.f);
    Value += Delta;
    DirtyStats.Add(StatTag);

    // Broadcast immediately so Achievement/Quest systems react in the same frame.
    FStatChangedEvent Event;
    Event.StatTag  = StatTag;
    Event.NewValue = Value;
    Event.Delta    = Delta;
    Event.PlayerId = GetOwner<APlayerState>()->GetUniqueId();

    UGameCoreEventBus::Get(this).Broadcast(
        TAG_Event_StatChanged,
        EGameCoreEventScope::Server,
        Event
    );
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
    // Triggers UPersistenceSubsystem to serialize this component to DB.
    UPersistenceSubsystem::Get(this).RequestFlush(this);
    DirtyStats.Empty();
}
```

Flush is also triggered on `EndPlay` to capture any stats modified in the final frame.

---

## ValidateDefinitions (non-shipping)

```cpp
void UStatComponent::ValidateDefinitions() const
{
    TSet<FGameplayTag> SeenTags;
    for (const UStatDefinition* Def : Definitions)
    {
        checkf(Def != nullptr, TEXT("UStatComponent: null UStatDefinition entry."));
        checkf(Def->StatTag.IsValid(), TEXT("UStatComponent: UStatDefinition has invalid StatTag."));
        checkf(!SeenTags.Contains(Def->StatTag),
            TEXT("UStatComponent: duplicate StatTag '%s'."), *Def->StatTag.ToString());
        SeenTags.Add(Def->StatTag);

        for (const UStatIncrementRule* Rule : Def->Rules)
        {
            checkf(Rule != nullptr, TEXT("UStatComponent: null Rule on definition '%s'."),
                *Def->StatTag.ToString());
            checkf(Rule->GetChannelTag().IsValid(),
                TEXT("UStatComponent: Rule on '%s' returns invalid ChannelTag."),
                *Def->StatTag.ToString());
        }
    }
}
```
