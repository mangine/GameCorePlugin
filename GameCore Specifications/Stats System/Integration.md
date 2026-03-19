# Stats System — Integration & Events

## FStatChangedEvent

Broadcast on `UGameCoreEventBus2` (server scope) every time a stat value changes. Downstream systems subscribe to this instead of coupling directly to `UStatComponent`.

```cpp
// GameCore module
USTRUCT(BlueprintType)
struct GAMECORE_API FStatChangedEvent
{
    GENERATED_BODY()

    // Which stat changed.
    UPROPERTY() FGameplayTag StatTag;

    // New cumulative value after the increment.
    UPROPERTY() float NewValue = 0.f;

    // The increment that caused this change.
    UPROPERTY() float Delta = 0.f;

    // The player whose stat changed.
    UPROPERTY() FUniqueNetIdRepl PlayerId;
};
```

Broadcast channel tag: `Event.Stat.Changed`

---

## Auto-Listener Wiring

`UStatComponent` auto-registers all listeners at `BeginPlay` from the `UStatIncrementRule` objects authored on each `UStatDefinition`. No game-module wiring subsystem is needed for event-driven stat increments.

The flow per incoming message:

```
GMS2 message arrives on ChannelTag
  → UStatComponent listener fires
  → Rule->ExtractIncrement(Payload) called
  → if Delta > 0: AddToStat(Def->StatTag, Delta)
    → requirements checked
    → RuntimeValues updated
    → FStatChangedEvent broadcast on GMS2
```

---

## Manual Increment (non-event-driven)

For increments not driven by a GMS2 message (e.g. quest completion reward, login bonus), call `AddToStat` directly. Server only.

```cpp
if (UStatComponent* Stats = PlayerState->FindComponentByClass<UStatComponent>())
{
    Stats->AddToStat(TAG_Stat_QuestsCompleted, 1.f);
}
```

---

## Achievement System Integration

```cpp
void UAchievementSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        StatChangedHandle = Bus->StartListening<FStatChangedEvent>(
            TAG_Event_StatChanged,
            this,
            [this](FGameplayTag, const FStatChangedEvent& Event)
            {
                EvaluateAchievements(Event.PlayerId, Event.StatTag, Event.NewValue);
            });
    }
}

void UAchievementSubsystem::Deinitialize()
{
    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        Bus->StopListening(StatChangedHandle);
    }
    Super::Deinitialize();
}
```

---

## Quest System Integration

Quest objectives subscribe to `TAG_Event_StatChanged` and filter by stat tag and player.

```cpp
void UStatObjective::BeginPlay()
{
    Super::BeginPlay();

    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        StatHandle = Bus->StartListening<FStatChangedEvent>(
            TAG_Event_StatChanged,
            this,
            [this](FGameplayTag, const FStatChangedEvent& Event)
            {
                if (Event.StatTag != TrackedStatTag) return;
                if (Event.PlayerId != OwningPlayerId) return;

                CurrentValue = Event.NewValue;
                if (CurrentValue >= TargetValue)
                    MarkComplete();
            });
    }
}

void UStatObjective::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        Bus->StopListening(StatHandle);
    }
    Super::EndPlay(Reason);
}
```

---

## Accessing a Stat Value

```cpp
float Kills = Stats->GetStat(TAG_Stat_EnemiesKilled);
```

`GetStat` is safe to call client-side for display purposes. For stats that need client visibility, replicate a subset via the standard `APlayerState` replication path — `RuntimeValues` itself is server-only.

---

## Persistence Notes

- `UStatComponent` flushes dirty stats on a repeating timer (`FlushIntervalSeconds`, default 10s).
- Flush is forced on `EndPlay` — no stat loss on clean disconnect.
- On player login, `UPersistenceSubsystem` deserializes `RuntimeValues` into the component before `BeginPlay` fires on any dependent system.
- Stats are per-character. Account-level stats require a separate account-scoped persistence record — out of scope for this system.

---

## Dependency Summary

| Dependency | Type |
|---|---|
| Requirement System (`URequirementSet`) | Optional per definition |
| Event Bus 2 (`UGameCoreEventBus2`) | Required — auto-listener registration + `FStatChangedEvent` broadcast |
| Serialization System (`IPersistableComponent`) | Required — persistence |
| Achievement System | None (subscribes to Event Bus 2; no direct dep) |
| Quest System | None (subscribes to Event Bus 2; no direct dep) |
