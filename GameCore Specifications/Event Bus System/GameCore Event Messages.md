# GameCore Event Messages

**Sub-page of:** [Event Bus System](../Event%20Bus%20System.md)

All message structs broadcast through `UGameCoreEventSubsystem` are defined in `GameCoreEventMessages.h`. Each event has exactly one struct. Structs are plain `USTRUCT`s — no UObject overhead, stack-allocated, passed by const reference.

---

## File Location

```
GameCore/Source/GameCore/EventBus/GameCoreEventMessages.h
```

---

## Channel Tag Definitions

Defined in `DefaultGameplayTags.ini` inside the `GameCore` module:

```ini
[/Script/GameplayTags.GameplayTagsList]
+GameplayTagList=(Tag="GameCoreEvent.Progression.LevelUp")
+GameplayTagList=(Tag="GameCoreEvent.Progression.XPChanged")
+GameplayTagList=(Tag="GameCoreEvent.Progression.PointPoolChanged")
+GameplayTagList=(Tag="GameCoreEvent.StateMachine.StateChanged")
+GameplayTagList=(Tag="GameCoreEvent.StateMachine.TransitionBlocked")
```

Native tag handles cached at module startup:

```cpp
// GameCoreEventTags.h
namespace GameCoreEventTags
{
    GAMECORE_API extern FGameplayTag Progression_LevelUp;
    GAMECORE_API extern FGameplayTag Progression_XPChanged;
    GAMECORE_API extern FGameplayTag Progression_PointPoolChanged;
    GAMECORE_API extern FGameplayTag StateMachine_StateChanged;
    GAMECORE_API extern FGameplayTag StateMachine_TransitionBlocked;
}
```

Cached via `UGameplayTagsManager::AddNativeGameplayTag` in `StartupModule`. Zero-cost lookup at broadcast sites.

---

## Progression Messages

### `FProgressionLevelUpMessage`

**Channel:** `GameCoreEvent.Progression.LevelUp`
**Origin:** `ULevelingComponent::ProcessLevelUp` — server only.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FProgressionLevelUpMessage
{
    GENERATED_BODY()

    UPROPERTY() TObjectPtr<APlayerState> PlayerState = nullptr;
    UPROPERTY() FGameplayTag ProgressionTag;
    UPROPERTY() int32 OldLevel = 0;
    UPROPERTY() int32 NewLevel = 0;
};
```

**Consumers:** quest system, achievement system, watcher integration adapter (replaces direct delegate binding to `ULevelingComponent`), UI level-up feedback, audio/VFX triggers.

---

### `FProgressionXPChangedMessage`

**Channel:** `GameCoreEvent.Progression.XPChanged`
**Origin:** `ULevelingComponent::ApplyXP` — server only.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FProgressionXPChangedMessage
{
    GENERATED_BODY()

    UPROPERTY() TObjectPtr<APlayerState> PlayerState = nullptr;
    UPROPERTY() FGameplayTag ProgressionTag;
    UPROPERTY() int32 NewXP = 0;
    UPROPERTY() int32 Delta = 0;   // Positive = gain, negative = penalty
};
```

> `Delta` is the final applied amount after `UXPReductionPolicy` — not the raw base XP.

---

### `FProgressionPointPoolChangedMessage`

**Channel:** `GameCoreEvent.Progression.PointPoolChanged`
**Origin:** `UPointPoolComponent::AddPoints` / `ConsumePoints` — server only.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FProgressionPointPoolChangedMessage
{
    GENERATED_BODY()

    UPROPERTY() TObjectPtr<APlayerState> PlayerState = nullptr;
    UPROPERTY() FGameplayTag PoolTag;
    UPROPERTY() int32 NewSpendable = 0;
    UPROPERTY() int32 Delta        = 0;   // Positive = added, negative = consumed
};
```

**Consumers:** skill tree UI, talent unlock systems, achievement trackers.

---

## State Machine Messages

### `FStateMachineStateChangedMessage`

**Channel:** `GameCoreEvent.StateMachine.StateChanged`
**Origin:** `UStateMachineComponent::EnterState` — fires on whichever authority the component runs.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FStateMachineStateChangedMessage
{
    GENERATED_BODY()

    // TWeakObjectPtr to avoid extending actor lifetime from a listener.
    UPROPERTY() TWeakObjectPtr<UStateMachineComponent> Component;

    // Cached for convenience — avoids GetOwner() at listener sites.
    UPROPERTY() TWeakObjectPtr<AActor> OwnerActor;

    UPROPERTY() FGameplayTag PreviousState;
    UPROPERTY() FGameplayTag NewState;

    // Lets listeners filter by machine type without casting.
    UPROPERTY() TObjectPtr<UStateMachineAsset> StateMachineAsset = nullptr;
};
```

**Consumers:** quest systems, NPC AI coordinators, ship system, UI state displays, audio state machines.

> `StateMachineAsset` lets listeners early-out without casting. A quest listener checks `if (Message.StateMachineAsset != ExpectedAsset) return;`.

---

### `FStateMachineTransitionBlockedMessage`

**Channel:** `GameCoreEvent.StateMachine.TransitionBlocked`
**Origin:** `UStateMachineComponent::IsTransitionPermitted` returns false.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FStateMachineTransitionBlockedMessage
{
    GENERATED_BODY()

    UPROPERTY() TWeakObjectPtr<UStateMachineComponent> Component;
    UPROPERTY() TWeakObjectPtr<AActor> OwnerActor;
    UPROPERTY() FGameplayTag BlockedTargetState;
    UPROPERTY() FGameplayTag CurrentState;
};
```

**Consumers:** AI retry logic, debug/telemetry tooling.

---

## Authoring Rules for New Messages

1. One struct per event. No shared payloads across unrelated events.
2. First field is always the originating context (`PlayerState` or `OwnerActor`) — listeners always need to know *who*.
3. Use `TWeakObjectPtr` for component and actor references — listeners may outlive the broadcaster.
4. Use `TObjectPtr` for data assets — they are never destroyed while the world is live.
5. Add the channel tag to `DefaultGameplayTags.ini` and the `GameCoreEventTags` namespace in the same commit as the struct.
