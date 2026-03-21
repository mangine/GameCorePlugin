# Active Event Registry

**Part of:** Event Bus System (`UGameCoreEventSubsystem`)  
**Used by:** Time Weather System and any system that registers global events

A lightweight extension to `UGameCoreEventSubsystem` that provides a tag-keyed registry of currently active events. Any system can register and query without coupling to the registering system.

---

## Motivation

GMS handles event **notifications** (fire-and-forget broadcast at activation/completion). The registry answers **point-in-time queries**: `"Is a storm active right now?"` without requiring the querying system to have subscribed at startup.

The registry holds **no lifecycle logic** — it is a pure presence tracker. Each owning system drives its own lifecycle.

---

## FActiveEventRecord

```cpp
USTRUCT()
struct FActiveEventRecord
{
    GENERATED_BODY()

    FGuid        EventId;
    FGameplayTag EventTag;
    double       RegisteredAtSeconds = 0.0; // FPlatformTime::Seconds() at registration
    float        ExpectedDuration    = 0.f; // 0 = indefinite
    TWeakObjectPtr<UObject> Instigator;
};
```

---

## API Addition to UGameCoreEventSubsystem

```cpp
// Added to UGameCoreEventSubsystem:

public:
    /**
     * Register an active event into the global registry.
     * Tag hierarchy is indexed — querying a parent tag matches all children.
     * @param EventTag   Tag identifying the event. E.g. Weather.Storm.Heavy
     * @param Duration   Expected duration in seconds. 0 = indefinite (must be manually unregistered).
     * @param Instigator Optional UObject for debug attribution.
     * @return FGuid handle for unregistration.
     */
    FGuid RegisterActiveEvent(
        FGameplayTag EventTag,
        float        Duration,
        UObject*     Instigator = nullptr);

    /**
     * Unregister an active event. Safe with invalid or already-removed GUID.
     */
    void UnregisterActiveEvent(FGuid EventId);

    /** True if any event matching EventTag (or a child tag) is currently active. */
    bool IsEventActive(FGameplayTag EventTag) const;

    /** All active event GUIDs whose tags match EventTag or its children. */
    TArray<FGuid> GetActiveEvents(FGameplayTag CategoryTag) const;

    /** All active event records. For debug/tooling use. */
    const TMap<FGuid, FActiveEventRecord>& GetAllActiveEvents() const;

private:
    TMap<FGuid, FActiveEventRecord> ActiveEventRegistry;
    FTimerHandle ExpiryTimerHandle;
```

---

## Implementation

```cpp
FGuid UGameCoreEventSubsystem::RegisterActiveEvent(
    FGameplayTag EventTag, float Duration, UObject* Instigator)
{
    FGuid Id = FGuid::NewGuid();
    FActiveEventRecord& Record = ActiveEventRegistry.Add(Id);
    Record.EventId             = Id;
    Record.EventTag            = EventTag;
    Record.RegisteredAtSeconds = FPlatformTime::Seconds();
    Record.ExpectedDuration    = Duration;
    Record.Instigator          = Instigator;
    return Id;
}

void UGameCoreEventSubsystem::UnregisterActiveEvent(FGuid EventId)
{
    ActiveEventRegistry.Remove(EventId); // no-op if not found
}

bool UGameCoreEventSubsystem::IsEventActive(FGameplayTag EventTag) const
{
    for (const auto& [Id, Record] : ActiveEventRegistry)
        if (Record.EventTag.MatchesTag(EventTag))
            return true;
    return false;
}

TArray<FGuid> UGameCoreEventSubsystem::GetActiveEvents(
    FGameplayTag CategoryTag) const
{
    TArray<FGuid> Result;
    for (const auto& [Id, Record] : ActiveEventRegistry)
        if (Record.EventTag.MatchesTag(CategoryTag))
            Result.Add(Id);
    return Result;
}
```

**Tag matching:** `MatchesTag` is hierarchical — querying `Weather.Storm` matches `Weather.Storm.Heavy` and `Weather.Storm.Light`.

---

## Expiry Sweep (Safety Net)

For events registered with non-zero `ExpectedDuration`, the subsystem sweeps expired records on a low-frequency timer to auto-clean stale registrations from systems that forgot to unregister. This is **not** the primary lifecycle path.

```cpp
// In UGameCoreEventSubsystem::Initialize:
GetWorld()->GetTimerManager().SetTimer(
    ExpiryTimerHandle,
    this, &UGameCoreEventSubsystem::SweepExpiredEvents,
    30.f, true);

void UGameCoreEventSubsystem::SweepExpiredEvents()
{
    double Now = FPlatformTime::Seconds();
    TArray<FGuid> Expired;
    for (const auto& [Id, Record] : ActiveEventRegistry)
    {
        if (Record.ExpectedDuration > 0.f &&
            (Now - Record.RegisteredAtSeconds) > Record.ExpectedDuration + 30.f)
            Expired.Add(Id);
    }
    for (FGuid Id : Expired)
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("ActiveEventRegistry: Auto-expiring stale event [%s]"),
            *ActiveEventRegistry[Id].EventTag.ToString());
        ActiveEventRegistry.Remove(Id);
    }
}
```
