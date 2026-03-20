# GameCore Event Messages

All message structs broadcast through `UGameCoreEventBus` are plain `USTRUCT`s — no UObject overhead, stack-allocated, wrapped in `FInstancedStruct` at the broadcast site.

**Message structs are defined in the header of the system that owns them**, not in a central file. This page is the index and documentation registry for all channels.

---

## Channel Tag Registration

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

Cached via `UGameplayTagsManager::Get().AddNativeGameplayTag(...)` in `FGameCoreModule::StartupModule`. Zero-cost lookup at broadcast sites — no string lookup at runtime.

---

## Authoring Rules for New Messages

1. **One struct per channel.** Never share a payload struct between two unrelated events.
2. **First field is always subject/instigator context.** Listeners always need to know *who*.
3. **Use `TWeakObjectPtr` for actors and components** — listeners may outlive the broadcaster.
4. **Use `TObjectPtr` for data assets** — they are never destroyed while the world is live.
5. **Add the channel tag to `DefaultGameplayTags.ini` and `GameCoreEventTags`** in the same commit as the struct.
6. **Declare `Scope` and `Origin` for every channel.** Document the client reaction path if scope is `ServerOnly`. Document the scope rationale if scope is `Both` or `ClientOnly`.
7. **Add a high-frequency warning** if the event can fire more than once per second under normal gameplay load.

---

## Progression Messages

**Struct defined in:** `Progression/LevelingComponent.h`

### `FProgressionLevelUpMessage`

| Field | Value |
|---|---|
| Channel | `GameCoreEvent.Progression.LevelUp` |
| Scope | `ServerOnly` |
| Origin | `ULevelingComponent::ProcessLevelUp` — server only |
| Client reaction | Via `FFastArraySerializer` replication of `ProgressionData` — no client-side broadcast |

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FProgressionLevelUpMessage
{
    GENERATED_BODY()

    /** Player who triggered the grant. May be nullptr for server-initiated grants. */
    UPROPERTY() TObjectPtr<APlayerState> Instigator = nullptr;

    /** Actor that leveled up (pawn, NPC, crew member, ship, etc.). */
    UPROPERTY() TObjectPtr<AActor> Subject = nullptr;

    UPROPERTY() FGameplayTag ProgressionTag;
    UPROPERTY() int32 OldLevel = 0;
    UPROPERTY() int32 NewLevel = 0;
};
```

**Consumers:** quest system, achievement system, UI level-up feedback, audio/VFX triggers.

---

### `FProgressionXPChangedMessage`

| Field | Value |
|---|---|
| Channel | `GameCoreEvent.Progression.XPChanged` |
| Scope | `ServerOnly` |
| Origin | `ULevelingComponent::ApplyXP` — server only |
| Client reaction | Via `FFastArraySerializer` replication of `ProgressionData` |

> ⚠️ **High-frequency warning.** This event can fire once per XP grant during combat. Call sites **must batch** XP application server-side (accumulate per frame or per threshold) before broadcasting — do not fire once per hit.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FProgressionXPChangedMessage
{
    GENERATED_BODY()

    /** Player who triggered the grant. May be nullptr for server-initiated grants. */
    UPROPERTY() TObjectPtr<APlayerState> Instigator = nullptr;

    /** Actor whose ULevelingComponent was mutated. */
    UPROPERTY() TObjectPtr<AActor> Subject = nullptr;

    UPROPERTY() FGameplayTag ProgressionTag;
    UPROPERTY() int32 NewXP = 0;
    UPROPERTY() int32 Delta = 0;  // Positive = gain, negative = penalty. Final value after UXPReductionPolicy.
};
```

**Consumers:** quest trackers, XP bar UI (via replication, not this event directly), achievement systems.

---

### `FProgressionPointPoolChangedMessage`

| Field | Value |
|---|---|
| Channel | `GameCoreEvent.Progression.PointPoolChanged` |
| Scope | `ServerOnly` |
| Origin | `UPointPoolComponent::AddPoints` / `ConsumePoints` — server only |
| Client reaction | Via replicated point pool data |

> ⚠️ **High-frequency warning.** Same batching responsibility as `XPChanged` if points are granted per-hit.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FProgressionPointPoolChangedMessage
{
    GENERATED_BODY()

    /** Actor that owns the pool (player pawn, NPC, ship, etc.). */
    UPROPERTY() TObjectPtr<AActor> Subject = nullptr;

    UPROPERTY() FGameplayTag PoolTag;
    UPROPERTY() int32 NewSpendable = 0;
    UPROPERTY() int32 Delta = 0;  // Positive = added, negative = consumed.
};
```

**Consumers:** skill tree UI, talent unlock systems, achievement trackers.

---

## State Machine Messages

**Struct defined in:** `StateMachine/StateMachineComponent.h`

### `FStateMachineStateChangedMessage`

| Field | Value |
|---|---|
| Channel | `GameCoreEvent.StateMachine.StateChanged` |
| Scope | `Both` |
| Origin | `UStateMachineComponent::EnterState` — fires on whichever authority the component runs |
| Scope rationale | State machine components declare `AuthorityMode` (`ServerOnly`/`ClientOnly`/`Both`). Broadcast scope matches that mode — a `ServerOnly` component fires only on the server |

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FStateMachineStateChangedMessage
{
    GENERATED_BODY()

    /** TWeakObjectPtr to avoid extending component lifetime from a listener. */
    UPROPERTY() TWeakObjectPtr<UStateMachineComponent> Component;

    /** Cached for convenience — avoids GetOwner() calls at listener sites. */
    UPROPERTY() TWeakObjectPtr<AActor> OwnerActor;

    UPROPERTY() FGameplayTag PreviousState;
    UPROPERTY() FGameplayTag NewState;

    /** Lets listeners filter by machine type without casting. */
    UPROPERTY() TObjectPtr<UStateMachineAsset> StateMachineAsset = nullptr;
};
```

> `StateMachineAsset` enables an early-out pattern at listener sites without casting:
> ```cpp
> if (Msg.StateMachineAsset != ExpectedAsset) return;
> ```

**Consumers:** quest systems, NPC AI coordinators, ship system, UI state displays, audio state machines.

---

### `FStateMachineTransitionBlockedMessage`

| Field | Value |
|---|---|
| Channel | `GameCoreEvent.StateMachine.TransitionBlocked` |
| Scope | `Both` |
| Origin | `UStateMachineComponent` transition evaluation returning false |
| Scope rationale | Same as `StateChanged` — follows component `AuthorityMode` |

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
