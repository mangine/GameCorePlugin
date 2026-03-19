# Migration Guide — GMS1 → GMS2

**Sub-page of:** [Event Bus System 2](../Event%20Bus%20System%202.md)

This guide covers how to migrate an existing GMS1 channel to GMS2 and how both buses coexist safely during the transition.

---

## Coexistence Rules

- `UGameCoreEventSubsystem` (GMS1) and `UGameCoreEventBus2` (GMS2) are independent `UWorldSubsystem` instances. They do not share state.
- A channel tag may exist on GMS1 only, GMS2 only, or both simultaneously during migration. There is no conflict.
- Listeners registered on GMS1 will not receive broadcasts from GMS2 on the same tag, and vice versa. Both must be updated atomically per channel when migrating.
- GMS1 is not removed until all channels and all listeners for those channels have been moved to GMS2 and verified.

---

## Migration Steps Per Channel

For each GMS1 channel being migrated:

### Step 1 — Update the broadcaster

**Before (GMS1):**
```cpp
FProgressionLevelUpMessage Msg;
Msg.NewLevel = 5;

if (UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this))
{
    Bus->Broadcast(GameCoreEventTags::Progression_LevelUp, Msg, EGameCoreEventScope::ServerOnly);
}
```

**After (GMS2):**
```cpp
FProgressionLevelUpMessage Msg;
Msg.NewLevel = 5;

if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
{
    Bus->Broadcast(
        GameCoreEventTags::Progression_LevelUp,
        FInstancedStruct::Make(Msg),
        EGameCoreEventScope::ServerOnly);
}
```

### Step 2 — Update all listeners

**Before (GMS1):**
```cpp
LevelUpHandle = Bus->StartListening<FProgressionLevelUpMessage>(
    GameCoreEventTags::Progression_LevelUp,
    this,
    &UMySystem::OnLevelUp);

void UMySystem::OnLevelUp(FGameplayTag Channel, const FProgressionLevelUpMessage& Msg)
{
    // use Msg directly
}
```

**After (GMS2):**
```cpp
LevelUpHandle = Bus->StartListening(
    GameCoreEventTags::Progression_LevelUp,
    this,
    [this](FGameplayTag Channel, const FInstancedStruct& Message)
    {
        if (const FProgressionLevelUpMessage* Msg = Message.GetPtr<FProgressionLevelUpMessage>())
        {
            // use Msg
        }
    });
```

### Step 3 — Verify, then remove GMS1 registration

Once broadcaster and all listeners are on GMS2 and tested, remove the GMS1 `StartListening` / `Broadcast` calls for that channel. The channel tag itself is reused — no tag change needed.

---

## Checklist

- [ ] Broadcaster updated to `UGameCoreEventBus2::Broadcast` with `FInstancedStruct::Make`
- [ ] All `StartListening` call sites updated to lambda form with `GetPtr<T>()` unwrap
- [ ] All `StopListening` calls updated to target the GMS2 handle
- [ ] No remaining GMS1 `StartListening` calls for this channel
- [ ] No remaining GMS1 `Broadcast` calls for this channel
- [ ] Tested in PIE with both server and client

---

## What Does Not Change

- Channel `FGameplayTag` values — identical between buses.
- `EGameCoreEventScope` values — shared enum, no change.
- Message struct definitions — `USTRUCT` payloads are unchanged; they are simply wrapped in `FInstancedStruct` at the broadcast site.
- `FGameplayMessageListenerHandle` type — same handle type; same `StopListening` call in `EndPlay`.
