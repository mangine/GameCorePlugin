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

## GMS Payload Convention

`UStatComponent` registers listeners expecting `FInstancedStruct` payloads on GMS channels. Your game-side message senders must broadcast an `FInstancedStruct` wrapping the actual message struct.

**Sender side (game module):**
```cpp
// When an enemy dies:
FEnemyKilledMessage Msg;
Msg.EnemyTag   = EnemyGameplayTag;
Msg.KillerActor = Killer;

FInstancedStruct Payload;
Payload.InitializeAs<FEnemyKilledMessage>(Msg);

UGameplayMessageSubsystem::Get(World).BroadcastMessage(
    TAG_GameplayMessage_Combat_EnemyKilled,
    Payload
);
```

If your messages are already broadcast as strongly-typed structs (not `FInstancedStruct`), wrap the broadcast at the call site or add a secondary `FInstancedStruct` broadcast in parallel. The stat system only needs the `FInstancedStruct` channel.

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

Quest objectives that track stats subscribe to `TAG_Event_StatChanged` and compare `Event.StatTag` against objective tags. No direct dependency on `UStatComponent`.

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

For stat increments that are not driven by a GMS message (e.g., reward on quest completion):

```cpp
// Server only.
APlayerState* PS = GetPlayerState();
if (UStatComponent* Stats = PS->FindComponentByClass<UStatComponent>())
{
    Stats->AddToStat(TAG_Stat_QuestsCompleted, 1.f);
}
```

---

## Accessing a Stat Value

```cpp
float Kills = Stats->GetStat(TAG_Stat_EnemiesKilled);
```

`GetStat` is safe to call client-side for display purposes. The value is replicated via the standard `APlayerState` replication path — add `RuntimeValues` to a replicated property or use a separate replicated subset if only certain stats need client visibility.

---

## Persistence Notes

- `UStatComponent` flushes dirty stats every 10 seconds by default (configurable via project settings or a `UPROPERTY` on the component).
- Flush is forced on `EndPlay` — no stat loss on clean disconnect.
- On player login, `UPersistenceSubsystem` deserializes `RuntimeValues` into the component before `BeginPlay` fires on any dependent system.
- Stats are per-character, not per-account. If account-level stats are needed (e.g. total playtime across characters), a separate account-scoped persistence record is required — out of scope for this system.

---

## Dependency Summary

| Dependency | Type |
|---|---|
| Requirement System (`URequirementSet`) | Optional per definition |
| Event Bus (`UGameCoreEventBus`) | Required — broadcasts `FStatChangedEvent` |
| GMS (`UGameplayMessageSubsystem`) | Required — message source for auto-rules |
| Serialization System (`IPersistableComponent`) | Required — persistence |
| Achievement System | None (subscribes to Event Bus; no direct dep) |
