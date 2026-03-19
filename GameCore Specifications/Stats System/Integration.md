# Stats System — Integration & Events

## FStatChangedEvent

Broadcast on the Event Bus (server scope) every time a stat value changes. Downstream systems subscribe to this instead of coupling directly to `UStatComponent`.

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

## GMS Integration — Game Module Responsibility

GMS requires typed listeners at compile time (`RegisterListener<T>`). `UStatComponent` does not auto-register any GMS listeners. The game module owns all GMS subscriptions and calls `AddToStat()` after receiving a typed message.

This means:
- No double-broadcasting of messages.
- No `FInstancedStruct` wrapping required.
- Full type safety — the game module reads payload fields directly.
- One GMS listener block per message type, registered once at startup (e.g. in a `UGameInstanceSubsystem` or `AGameMode`).

### Game Module Wiring Example

```cpp
// In UMyStatWiringSubsystem : UGameInstanceSubsystem (game module)
// Or in AGameMode::BeginPlay — wherever your GMS subscriptions live.

void UMyStatWiringSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UGameplayMessageSubsystem& GMS = UGameplayMessageSubsystem::Get(this);

    // Fixed increment — 1 kill = 1 unit
    EnemyKilledHandle = GMS.RegisterListener<FEnemyKilledMessage>(
        TAG_GameplayMessage_Combat_EnemyKilled,
        [this](FGameplayTag, const FEnemyKilledMessage& Msg)
        {
            if (UStatComponent* Stats = GetStatComponentForPlayer(Msg.KillerPlayerId))
                Stats->AddToStat(TAG_Stat_EnemiesKilled, 1.f);
        }
    );

    // Payload-driven increment — use damage value from typed struct
    DamageDealtHandle = GMS.RegisterListener<FDamageDealtMessage>(
        TAG_GameplayMessage_Combat_DamageDealt,
        [this](FGameplayTag, const FDamageDealtMessage& Msg)
        {
            if (UStatComponent* Stats = GetStatComponentForPlayer(Msg.InstigatorPlayerId))
                Stats->AddToStat(TAG_Stat_TotalDamageDealt, Msg.DamageAmount);
        }
    );
}

void UMyStatWiringSubsystem::Deinitialize()
{
    EnemyKilledHandle.Unregister();
    DamageDealtHandle.Unregister();
    Super::Deinitialize();
}
```

### Helper: GetStatComponentForPlayer

```cpp
UStatComponent* UMyStatWiringSubsystem::GetStatComponentForPlayer(const FUniqueNetIdRepl& PlayerId)
{
    // Iterate PlayerStates to find the matching one.
    for (APlayerState* PS : GetWorld()->GetGameState()->PlayerArray)
    {
        if (PS && PS->GetUniqueId() == PlayerId)
            return PS->FindComponentByClass<UStatComponent>();
    }
    return nullptr;
}
```

---

## Achievement System Integration

```cpp
// In UAchievementSubsystem::Initialize()
UGameCoreEventBus::Get(this).Subscribe<FStatChangedEvent>(
    TAG_Event_StatChanged,
    EGameCoreEventScope::Server,
    this,
    &UAchievementSubsystem::OnStatChanged
);

void UAchievementSubsystem::OnStatChanged(const FStatChangedEvent& Event)
{
    EvaluateAchievements(Event.PlayerId, Event.StatTag, Event.NewValue);
}
```

---

## Quest System Integration

Quest objectives subscribe to `TAG_Event_StatChanged` and compare against their tracked stat tag. No direct dependency on `UStatComponent`.

```cpp
void UStatObjective::OnStatChanged(const FStatChangedEvent& Event)
{
    if (Event.StatTag != TrackedStatTag) return;
    if (Event.PlayerId != OwningPlayerId) return;

    CurrentValue = Event.NewValue;
    if (CurrentValue >= TargetValue)
        MarkComplete();
}
```

---

## Manual Increment (non-event-driven)

For increments not driven by a GMS message (e.g. quest completion reward, login bonus):

```cpp
// Server only.
if (UStatComponent* Stats = PlayerState->FindComponentByClass<UStatComponent>())
{
    Stats->AddToStat(TAG_Stat_QuestsCompleted, 1.f);
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

- `UStatComponent` flushes dirty stats on a repeating timer (`FlushIntervalSeconds`, default 10s, configurable per component instance).
- Flush is forced on `EndPlay` — no stat loss on clean disconnect.
- On player login, `UPersistenceSubsystem` deserializes `RuntimeValues` into the component before `BeginPlay` fires on any dependent system.
- Stats are per-character. Account-level stats (e.g. total playtime across characters) require a separate account-scoped persistence record — out of scope for this system.

---

## Dependency Summary

| Dependency | Type |
|---|---|
| Requirement System (`URequirementSet`) | Optional per definition |
| Event Bus (`UGameCoreEventBus`) | Required — broadcasts `FStatChangedEvent` |
| GMS (`UGameplayMessageSubsystem`) | Game module only — not used by GameCore directly |
| Serialization System (`IPersistableComponent`) | Required — persistence |
| Achievement System | None (subscribes to Event Bus; no direct dep) |
