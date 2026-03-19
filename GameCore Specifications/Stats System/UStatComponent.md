# UStatComponent

## Role

Actor component that lives on `APlayerState`. Owns runtime stat values for one player and persists them via `IPersistableComponent`.

All stat mutation is server-authoritative. Methods that mutate state check `GetOwner()->HasAuthority()` and early-out on clients.

> **GMS Note:** `UStatComponent` does **not** auto-register GMS listeners. GMS requires typed listeners at compile time and does not support untyped/wildcard registration. The game module is responsible for all GMS subscriptions and calls `AddToStat()` directly. See [Integration](./Integration.md).

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
    // Serves as the authoritative list of which stats exist for this player.
    UPROPERTY(EditDefaultsOnly, Category="Stats")
    TArray<TObjectPtr<UStatDefinition>> Definitions;

    // Configurable flush interval in seconds. Default: 10s.
    UPROPERTY(EditDefaultsOnly, Category="Stats", meta=(ClampMin="1.0"))
    float FlushIntervalSeconds = 10.f;

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

    // Flush timer handle.
    FTimerHandle FlushTimerHandle;

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
        return;

    // Flush dirty stats on a repeating timer.
    GetWorld()->GetTimerManager().SetTimer(
        FlushTimerHandle,
        this,
        &UStatComponent::FlushDirtyStats,
        FlushIntervalSeconds,
        true
    );
}
```

---

## AddToStat

```cpp
void UStatComponent::AddToStat(FGameplayTag StatTag, float Delta)
{
    if (!GetOwner()->HasAuthority()) return;
    if (Delta <= 0.f) return;

    // Check requirements if a matching definition exists.
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

> **Note:** The requirements loop is O(n) over `Definitions`. For projects with large definition arrays, consider building a `TMap<FGameplayTag, UStatDefinition*>` cache at `BeginPlay`.

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

Flush is also triggered on `EndPlay` to capture any stats modified in the final frame.

```cpp
void UStatComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    FlushDirtyStats();
    GetWorld()->GetTimerManager().ClearTimer(FlushTimerHandle);
    Super::EndPlay(Reason);
}
```

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
