# Event Bus System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The Event Bus System (`UGameCoreEventBus2`) is a `UWorldSubsystem` wrapper around Unreal's `UGameplayMessageSubsystem` (GMS). It provides a single, cached broadcast point for all cross-system events emitted by GameCore modules, using `FInstancedStruct` as the universal payload type to enable both strongly-typed and runtime-dynamic message dispatch.

---

## Purpose

Delegates are the right tool for intra-system wiring — they are synchronous, typed, and zero-overhead when no listeners are bound. However, they force every external consumer to take a direct reference to the component that owns the delegate, creating coupling that undermines GameCore's module-independence guarantee.

`UGameCoreEventBus2` solves this by acting as a single, world-scoped broadcast bus. Any system can broadcast a typed message to a named channel. Any other system can listen to that channel with zero knowledge of the broadcaster.

---

## Design Rules

- **Delegates stay for intra-system wiring.** A component may still fire its own delegate and call `Broadcast` in the same mutation — the delegate is for systems inside the same module; the bus is for everything else.
- **One struct per event type.** Never share a payload struct between two unrelated events.
- **Channel tags follow `GameCoreEvent.<System>.<Event>`.** Defined in `DefaultGameplayTags.ini` inside the owning module. Never in a central tags file.
- **Every channel must declare its scope.** The broadcaster decides scope — the bus enforces it. Document scope and origin in `GameCore Event Messages.md`. Default is `ServerOnly`.
- **Never call `UGameplayMessageSubsystem` directly** from GameCore module code. Always go through `UGameCoreEventBus2`.
- **`ClientOnly` requires prior replication.** A client-side broadcast is a notification layer only — the data it references must already exist on the client via `FFastArraySerializer` or a replicated property before the broadcast fires.
- **Do not cache `UGameCoreEventBus2` as a member.** Subsystem lifetime is tied to the world; components may outlive a world transition. Use `UGameCoreEventBus2::Get(this)` inline at call sites.

---

## File Layout

```
GameCore/
└── Source/
    └── GameCore/
        └── EventBus2/
            ├── GameCoreEventBus2.h
            └── GameCoreEventBus2.cpp
```

Message structs are owned by their originating systems, not by the bus itself.

---

## Module Dependencies

```csharp
// GameCore.Build.cs
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameplayTags",
    "GameplayMessageRuntime",
    "StructUtils"               // FInstancedStruct
});
```

---

## Quick Usage Guide

### Broadcasting

```cpp
// External call sites — construct FInstancedStruct explicitly.
FMyCustomMessage Msg;
Msg.SomeField = 42;

if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
{
    Bus->Broadcast(
        MyTags::SomeChannel,
        FInstancedStruct::Make(Msg),
        EGameCoreEventScope::ServerOnly);
}

// Internal GameCore systems — use the typed overload.
Bus->Broadcast<FMyCustomMessage>(MyTags::SomeChannel, Msg, EGameCoreEventScope::ServerOnly);
```

### Listening

```cpp
FGameplayMessageListenerHandle Handle;

void UMySystem::BeginPlay()
{
    Super::BeginPlay();

    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        Handle = Bus->StartListening<FMyCustomMessage>(
            MyTags::SomeChannel,
            this,
            [this](FGameplayTag, const FMyCustomMessage& Msg)
            {
                // Use Msg.SomeField
            });
    }
}

void UMySystem::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
        Bus->StopListening(Handle);
    Super::EndPlay(Reason);
}
```

### Dynamic Registration (runtime-unknown type)

```cpp
// When the struct type is only known at runtime — use the raw FInstancedStruct overload.
Handle = Bus->StartListening(
    MyTags::SomeChannel,
    this,
    [](FGameplayTag, const FInstancedStruct& Message)
    {
        if (const FMyTypeA* A = Message.GetPtr<FMyTypeA>()) { /* ... */ }
        else if (const FMyTypeB* B = Message.GetPtr<FMyTypeB>()) { /* ... */ }
    });
```

---

## Channel Tag Conventions

All GameCore channels follow `GameCoreEvent.<System>.<Event>`. Defined per-module in `DefaultGameplayTags.ini`.

Every channel entry in `GameCore Event Messages.md` must declare its **Scope** and **Origin machine**.

```
GameCoreEvent
  ├── Progression
  │     ├── LevelUp          [ServerOnly]
  │     ├── XPChanged        [ServerOnly]
  │     └── PointPoolChanged [ServerOnly]
  └── StateMachine
        ├── StateChanged     [Both — depends on component AuthorityMode]
        └── TransitionBlocked[Both — depends on component AuthorityMode]
```

---

## Known Limitations

- **GMS is synchronous.** `BroadcastMessage` calls all listeners inline before returning. Heavy listener logic on a hot-path broadcast (e.g. `XPChanged`) should be deferred by the listener, not by the bus.
- **`XPChanged` and `PointPoolChanged` can be high-frequency.** Batching must be applied at the call site — accumulate server-side, broadcast once per frame or threshold. The bus does not throttle.
- **Scope is not a replication primitive.** The bus never sends data across the network. Scope only guards against broadcasting on the wrong machine.
- **No cross-world channels.** Each `UGameCoreEventBus2` instance is world-scoped. Broadcasts do not cross world boundaries.

---

## Sub-Pages

| Sub-Page | Covers |
|---|---|
| [UGameCoreEventBus2](Event%20Bus%20System%202/UGameCoreEventBus2.md) | Class definition, all methods, implementation notes |
| [Scope Guard](Event%20Bus%20System%202/Scope%20Guard.md) | `EGameCoreEventScope` enum, evaluation rules |
| [GameCore Event Messages](Event%20Bus%20System%202/GameCore%20Event%20Messages.md) | All message structs and channel tag definitions |
