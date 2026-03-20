# Event Bus System — Usage Guide

---

## When to Use the Bus

| Scenario | Correct tool |
|---|---|
| Component notifying its own owner | `DECLARE_DELEGATE` / `DECLARE_DYNAMIC_MULTICAST_DELEGATE` |
| Cross-system notification, broadcaster and listener must not know each other | `UGameCoreEventBus` |
| Listening to one typed channel | `UGameCoreEventBus::StartListening<T>` |
| Listening to one or more channels with captured closure context | `UGameCoreEventWatcher::Register` |
| Reactive re-evaluation across many tags under one handle | `UGameCoreEventWatcher::Register` with `FGameplayTagContainer` |

---

## Rule Summary

- **Never call `UGameplayMessageSubsystem` directly.** Always go through `UGameCoreEventBus`.
- **Never cache `UGameCoreEventBus` or `UGameCoreEventWatcher` as member fields.** Call `::Get(this)` inline.
- **Always store listener handles** and call `StopListening` / `Unregister` in `EndPlay` or equivalent teardown.
- **Always declare `Scope` explicitly.** The default is `ServerOnly` — this is intentional and conservative.
- **Channel tags follow `GameCoreEvent.<Module>.<Event>`.** Define in `DefaultGameplayTags.ini` of the owning module.
- **One struct per channel.** Never share a payload struct across unrelated events.
- **Subscribe to leaf tags only.** `GameCoreEvent.Combat` does not receive `GameCoreEvent.Combat.EnemyKilled`.

---

## Broadcasting

### External call site (standard pattern)

```cpp
// Construct the payload
FMyEventMessage Msg;
Msg.Subject = MyActor;
Msg.Value   = 42;

// Broadcast
UGameCoreEventBus::Get(this)->Broadcast(
    GameCoreEventTags::MyModule_MyEvent,
    FInstancedStruct::Make(Msg),
    EGameCoreEventScope::ServerOnly);
```

### Internal GameCore system (typed overload)

```cpp
// Used only inside GameCore plugin systems, e.g. ULevelingComponent
FProgressionLevelUpMessage Msg;
Msg.Subject  = OwnerActor;
Msg.OldLevel = PreviousLevel;
Msg.NewLevel = CurrentLevel;

Bus->Broadcast<FProgressionLevelUpMessage>(
    GameCoreEventTags::Progression_LevelUp,
    Msg,
    EGameCoreEventScope::ServerOnly);
```

> `Broadcast<T>` is a thin wrapper that calls `FInstancedStruct::Make(Message)` and forwards to the raw overload. Prefer the raw overload at external call sites for clarity.

---

## Listening via `UGameCoreEventBus` (one channel, typed)

Best for systems that listen to exactly one channel and want a strongly-typed callback.

```cpp
// .h
FGameplayMessageListenerHandle LevelUpHandle;

// BeginPlay or Initialize
if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
{
    LevelUpHandle = Bus->StartListening<FProgressionLevelUpMessage>(
        GameCoreEventTags::Progression_LevelUp,
        this,
        [this](FGameplayTag, const FProgressionLevelUpMessage& Msg)
        {
            OnLevelUp(Msg.Subject, Msg.NewLevel);
        });
}

// EndPlay
if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    Bus->StopListening(LevelUpHandle);
```

---

## Listening via `UGameCoreEventBus` (one channel, raw)

Use when the message type is only known at runtime or multiple struct types may appear on the same channel.

```cpp
Handle = Bus->StartListening(
    MyTags::SomeChannel,
    this,
    [](FGameplayTag, const FInstancedStruct& Payload)
    {
        if (const FTypeA* A = Payload.GetPtr<FTypeA>()) { /* ... */ }
        else if (const FTypeB* B = Payload.GetPtr<FTypeB>()) { /* ... */ }
    });
```

---

## Listening via `UGameCoreEventWatcher` (single tag)

Best for systems that need closure context (e.g. `this` is not a `UObject`).

```cpp
// .h
FEventWatchHandle WatchHandle;

// Register
TWeakObjectPtr<UMySystem> WeakThis = this;

WatchHandle = UGameCoreEventWatcher::Get(this)->Register(
    this,
    GameCoreEventTags::Progression_LevelUp,
    EGameCoreEventScope::ServerOnly,
    [WeakThis](FGameplayTag Tag, const FInstancedStruct& Payload)
    {
        if (UMySystem* Self = WeakThis.Get())
            Self->OnLevelUp(Tag, Payload);
    });

// Teardown
UGameCoreEventWatcher::Get(this)->Unregister(WatchHandle);
```

---

## Listening via `UGameCoreEventWatcher` (multiple tags, one handle)

```cpp
FGameplayTagContainer Tags;
Tags.AddTag(GameCoreEventTags::Progression_LevelUp);
Tags.AddTag(GameCoreEventTags::Progression_XPChanged);

TWeakObjectPtr<UMySystem> WeakThis = this;

WatchHandle = UGameCoreEventWatcher::Get(this)->Register(
    this,
    Tags,
    EGameCoreEventScope::ServerOnly,
    [WeakThis](FGameplayTag Tag, const FInstancedStruct& Payload)
    {
        if (UMySystem* Self = WeakThis.Get())
            Self->OnProgressionEvent(Tag, Payload);
    });

// Single Unregister removes all tag subscriptions.
UGameCoreEventWatcher::Get(this)->Unregister(WatchHandle);
```

---

## Defining a New Channel

### 1. Add the tag to `DefaultGameplayTags.ini` (owning module)

```ini
[/Script/GameplayTags.GameplayTagsList]
+GameplayTagList=(Tag="GameCoreEvent.MyModule.MyEvent")
```

### 2. Declare and cache the native handle in `GameCoreEventTags.h / .cpp`

```cpp
// GameCoreEventTags.h
namespace GameCoreEventTags
{
    GAMECORE_API extern FGameplayTag MyModule_MyEvent;
}

// GameCoreEventTags.cpp — inside FGameCoreModule::StartupModule()
GameCoreEventTags::MyModule_MyEvent =
    UGameplayTagsManager::Get().AddNativeGameplayTag(
        TEXT("GameCoreEvent.MyModule.MyEvent"));
```

### 3. Define the message struct (in the owning system's header)

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FMyEventMessage
{
    GENERATED_BODY()

    // Always include subject/instigator context first.
    UPROPERTY() TWeakObjectPtr<AActor> Subject;
    UPROPERTY() int32 Value = 0;
};
```

### 4. Document it in [GameCore Event Messages](GameCore%20Event%20Messages.md)

Every new channel entry **must declare**:
- `Scope` (`ServerOnly` / `ClientOnly` / `Both`)
- `Origin` machine and originating function
- Client reaction path if `ServerOnly`
- Scope rationale if `Both` or `ClientOnly`
- High-frequency warning if the event can fire more than once per second under normal load

---

## Teardown Checklist

| Where registered | Handle type | Teardown call |
|---|---|---|
| `UGameCoreEventBus::StartListening<T>` | `FGameplayMessageListenerHandle` | `Bus->StopListening(Handle)` |
| `UGameCoreEventBus::StartListening` (raw) | `FGameplayMessageListenerHandle` | `Bus->StopListening(Handle)` |
| `UGameCoreEventWatcher::Register` | `FEventWatchHandle` | `Watcher->Unregister(Handle)` |

All teardown calls are safe with an invalid/default handle.
