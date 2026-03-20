# Event Bus System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The Event Bus System provides two complementary subsystems for cross-system event communication:

- **`UGameCoreEventBus`** — the broadcast layer. Any system broadcasts a typed `FInstancedStruct` payload on a `FGameplayTag` channel. Any system listens to a specific channel.
- **`UGameCoreEventWatcher`** — the subscription routing layer. Systems register closures against one or more tags. When a matching event arrives, registered callbacks fire immediately with the raw payload. Manages bus handle lifecycle automatically.

Delegates remain the right tool for intra-system wiring. The bus and watcher are for cross-system communication where the broadcaster and listener must not know about each other.

---

## Design Rules

- **Delegates for intra-system, bus for cross-system.** A component fires its own delegate for internal consumers; it calls `Broadcast` for external ones.
- **One struct per event type.** Never share a payload struct between two unrelated events.
- **Channel tags follow `GameCoreEvent.<Module>.<Event>`.** Defined in `DefaultGameplayTags.ini` in the owning module. Never in a central file.
- **Every channel declares its scope.** The broadcaster decides scope — the bus enforces it. Document in `GameCore Event Messages.md`. Default is `ServerOnly`.
- **Never call `UGameplayMessageSubsystem` directly.** Always go through `UGameCoreEventBus`.
- **`ClientOnly` requires prior replication.** The broadcast is a notification layer only — data must already exist on the client before the broadcast fires.
- **Do not cache `UGameCoreEventBus` or `UGameCoreEventWatcher` as members.** Use `::Get(this)` inline at call sites.

---

## When to use `UGameCoreEventBus` vs `UGameCoreEventWatcher`

| Scenario | Use |
|---|---|
| Broadcasting an event | `UGameCoreEventBus::Broadcast` |
| Listening to one specific channel, typed callback | `UGameCoreEventBus::StartListening<T>` |
| Listening to one or more channels, raw callback, with captured context | `UGameCoreEventWatcher::Register` |
| System needs to manage many subscriptions under one handle | `UGameCoreEventWatcher::Register` with `FGameplayTagContainer` |
| Requirement system reactive evaluation | `UGameCoreEventWatcher` via `URequirementWatchHelper` |

---

## File Layout

```
GameCore/Source/GameCore/EventBus/
  ├── GameCoreEventBus.h / .cpp
  └── GameCoreEventWatcher.h / .cpp
```

---

## Module Dependencies

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameplayTags",
    "GameplayMessageRuntime",
    "StructUtils"
});
```

---

## Quick Usage

### Broadcasting

```cpp
FMyEvent Msg;
Msg.Value = 42;

UGameCoreEventBus::Get(this)->Broadcast(
    MyTags::MyChannel,
    FInstancedStruct::Make(Msg),
    EGameCoreEventScope::ServerOnly);
```

### Listening via EventBus (typed, one channel)

```cpp
Handle = Bus->StartListening<FMyEvent>(MyTags::MyChannel, this,
    [this](FGameplayTag, const FMyEvent& Msg) { /* ... */ });
```

### Listening via EventWatcher (raw, closure with context)

```cpp
TWeakObjectPtr<UMySystem> WeakThis = this;
FMyContext Ctx = BuildContext();

WatchHandle = UGameCoreEventWatcher::Get(this)->Register(this, MyTags::MyChannel,
    [WeakThis, Ctx](FGameplayTag Tag, const FInstancedStruct& Payload)
    {
        if (UMySystem* Self = WeakThis.Get())
            Self->HandleEvent(Ctx, Tag, Payload);
    });

// At teardown:
UGameCoreEventWatcher::Get(this)->Unregister(WatchHandle);
```

---

## Known Limitations

- **GMS is synchronous.** All listeners fire inline during `Broadcast`. Defer heavy work inside callbacks.
- **No parent tag subscription.** Exact tag matching only. `GameCoreEvent.Combat` does not receive `GameCoreEvent.Combat.EnemyKilled`. Subscribe to the specific leaf tag. See [UGameCoreEventBus](Event%20Bus%20System/UGameCoreEventBus.md) for a potential future `StartListeningToTagHierarchy` approach.
- **No cross-world channels.** Bus and watcher are world-scoped.
- **Scope is not a replication primitive.** Scope guards against wrong-machine broadcast only — it does not send data over the network.
- **High-frequency channels must batch at call site.** The bus does not throttle. `XPChanged` and similar channels must accumulate server-side and broadcast once per frame or threshold.

---

## Sub-Pages

| Sub-Page | Covers |
|---|---|
| [UGameCoreEventBus](Event%20Bus%20System/UGameCoreEventBus.md) | Class definition, broadcast, listen, implementation |
| [UGameCoreEventWatcher](Event%20Bus%20System/UGameCoreEventWatcher.md) | Generic subscription routing, handle lifecycle, re-entrancy |
| [Scope Guard](Event%20Bus%20System/Scope%20Guard.md) | `EGameCoreEventScope` enum, evaluation rules |
| [GameCore Event Messages](Event%20Bus%20System/GameCore%20Event%20Messages.md) | All message structs and channel tag definitions |
