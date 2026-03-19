# GameCore Event Messages

**Sub-page of:** [Event Bus System](../Event%20Bus%20System.md)

All message structs broadcast through `UGameCoreEventBus` are plain `USTRUCT`s — no UObject overhead, stack-allocated, wrapped into `FInstancedStruct` at the broadcast site. Message structs are defined alongside the system that owns them, not in a central file.

Every channel entry below declares its **Scope** (`EGameCoreEventScope`) and **Origin machine**. These are mandatory fields for all channels.

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
**Scope:** `ServerOnly`
**Origin:** `ULevelingComponent::ProcessLevelUp` — server only.
**Client reaction:** Clients react via `FFastArraySerializer` replication of `ProgressionData`. No client-side broadcast is fired for this event.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FProgressionLevelUpMessage
{
    GENERATED_BODY()

    // The player who triggered the grant. May be nullptr for server-initiated grants.
    UPROPERTY() TObjectPtr<APlayerState> Instigator = nullptr;

    // The Actor that leveled up (pawn, NPC, crew member, ship, etc.).
    UPROPERTY() TObjectPtr<AActor> Subject = nullptr;

    UPROPERTY() FGameplayTag ProgressionTag;
    UPROPERTY() int32 OldLevel = 0;
    UPROPERTY() int32 NewLevel = 0;
};
```

**Consumers:** quest system, achievement system, watcher integration adapter, UI level-up feedback, audio/VFX triggers.

---

### `FProgressionXPChangedMessage`

**Channel:** `GameCoreEvent.Progression.XPChanged`
**Scope:** `ServerOnly`
**Origin:** `ULevelingComponent::ApplyXP` — server only.
**Client reaction:** Clients react via `FFastArraySerializer` replication of `ProgressionData`.

> **High-frequency warning.** This event can fire once per XP grant during combat. Call sites must batch XP application server-side (accumulate per frame or per threshold) before broadcasting — do not fire once per hit.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FProgressionXPChangedMessage
{
    GENERATED_BODY()

    // The player who triggered the grant. May be nullptr for server-initiated grants.
    UPROPERTY() TObjectPtr<APlayerState> Instigator = nullptr;

    // The Actor whose ULevelingComponent was mutated.
    UPROPERTY() TObjectPtr<AActor> Subject = nullptr;

    UPROPERTY() FGameplayTag ProgressionTag;
    UPROPERTY() int32 NewXP = 0;
    UPROPERTY() int32 Delta = 0;   // Positive = gain, negative = penalty
};
```

> `Delta` is the final applied amount after `UXPReductionPolicy` — not the raw base XP.

---

### `FProgressionPointPoolChangedMessage`

**Channel:** `GameCoreEvent.Progression.PointPoolChanged`
**Scope:** `ServerOnly`
**Origin:** `UPointPoolComponent::AddPoints` / `ConsumePoints` — server only.
**Client reaction:** Clients react via replicated point pool data.

> **High-frequency warning.** Same batching responsibility as `XPChanged` if points are granted per-hit.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FProgressionPointPoolChangedMessage
{
    GENERATED_BODY()

    // The Actor that owns the pool (may be a player pawn, NPC, ship, etc.).
    UPROPERTY() TObjectPtr<AActor> Subject = nullptr;

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
**Scope:** `Both`
**Origin:** `UStateMachineComponent::EnterState` — fires on whichever authority the component runs.
**Scope rationale:** State machine components declare an `AuthorityMode` (`ServerOnly`, `ClientOnly`, `Both`). The broadcast scope matches the mode — a `ServerOnly` component fires only on the server; a `Both` component fires on both machines independently.

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

> `StateMachineAsset` lets listeners early-out without casting: `if (Message.StateMachineAsset != ExpectedAsset) return;`

---

### `FStateMachineTransitionBlockedMessage`

**Channel:** `GameCoreEvent.StateMachine.TransitionBlocked`
**Scope:** `Both`
**Origin:** `UStateMachineComponent::IsTransitionPermitted` returns false.
**Scope rationale:** Same as `StateChanged` — scope follows the component's `AuthorityMode`.

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
2. First field is always the originating context (`Instigator`/`Subject` or `OwnerActor`) — listeners always need to know *who*.
3. Use `TWeakObjectPtr` for component and actor references — listeners may outlive the broadcaster.
4. Use `TObjectPtr` for data assets — they are never destroyed while the world is live.
5. Add the channel tag to `DefaultGameplayTags.ini` and the `GameCoreEventTags` namespace in the same commit as the struct.
6. **Declare `Scope` and `Origin` machine for every channel.** Document the client reaction path if scope is `ServerOnly`. Document the scope rationale if scope is `Both` or `ClientOnly`.
7. Add a **high-frequency warning** if the event can fire more than once per second under normal gameplay load.
