# Event Bus System 2

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

Event Bus System 2 (`UGameCoreEventBus2`) is a `UWorldSubsystem` that mirrors Event Bus System 1 (`UGameCoreEventSubsystem`) in all architectural decisions — same `FGameplayTag` channels, same scope guard, same static accessor pattern — but uses a single strongly-typed `FInstancedStruct` as the universal message payload instead of a per-channel template type.

This enables **dynamic registration**: a broadcaster can wrap any `USTRUCT` into an `FInstancedStruct` and broadcast it without the channel being known to the bus at compile time. The listener receives the `FInstancedStruct` and unwraps it with `Get<T>()`.

Both systems coexist during migration. Neither depends on the other. GMS1 is not removed.

---

## Why a Second Bus

GMS1 is strongly typed per channel — `StartListening<T>` binds the callback to a specific struct type via `UScriptStruct`. This is correct for well-known system events (level-up, state change, etc.) but requires all message types to be known at the point of listener registration.

GMS2 solves cases where the message type is only known at runtime — for example, a system that dynamically registers handlers keyed by `FGameplayTag` without compile-time knowledge of the payload struct. The payload identity is carried inside `FInstancedStruct` and interrogated at runtime with `GetScriptStruct()` or `Get<T>()`.

---

## Key Decisions

| Decision | Rationale |
|---|---|
| `FInstancedStruct` as universal payload | Carries `UScriptStruct*` + owned memory; matches GMS's internal layout. No UObject overhead. |
| `FGameplayTag` channels retained | Same routing semantics as GMS1. No new channel primitive needed. |
| `TFunction` callback instead of member pointer | No template `T` to bind against; `TFunction` captures the lambda or method uniformly. |
| `EGameCoreEventScope` scope guard | Identical to GMS1. Enforces server/client correctness at broadcast site. |
| `FGameplayMessageListenerHandle` for unregistration | Reuses the existing GMS handle type; no new handle type needed. |
| `InitializeDependency<UGameplayMessageSubsystem>` | Guarantees GMS is alive before `UGameCoreEventBus2::Initialize` runs. |
| Subsystem named `UGameCoreEventBus2` | Mirrors GMS1 naming convention (`UGameCoreEventSubsystem`), adds `2` suffix for migration clarity. |
| GMS1 (`UGameCoreEventSubsystem`) renamed alias | Class is now also referred to as `UGameCoreEventBus` in documentation for clarity; no code rename needed unless team agrees. |

---

## Requirements

To recreate this module from scratch a developer must:

1. Create `UGameCoreEventBus2 : public UWorldSubsystem`.
2. In `Initialize`, call `Collection.InitializeDependency<UGameplayMessageSubsystem>()` then cache `GMS`.
3. Implement `Broadcast(FGameplayTag, FInstancedStruct, EGameCoreEventScope)` — scope guard first, then `GMS->BroadcastMessage<FInstancedStruct>(Channel, Message)`.
4. Implement `StartListening(FGameplayTag, UObject*, TFunction<void(FGameplayTag, const FInstancedStruct&)>)` — calls `GMS->RegisterListener<FInstancedStruct>` forwarding the `TFunction` via a lambda capture.
5. Implement `StopListening(FGameplayMessageListenerHandle&)` — unregisters and resets the handle.
6. Implement `static UGameCoreEventBus2* Get(const UObject*)` — same `GEngine->GetWorldFromContextObject` pattern as GMS1.
7. Add `StructUtils` to module `PublicDependencyModuleNames` (provides `FInstancedStruct`).
8. Ensure `GameplayMessageRuntime` is also in dependencies (provides `FGameplayMessageListenerHandle`).

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

No `GameCoreEventMessages` file for GMS2 — message structs remain owned by their originating systems. GMS2 does not define any message types.

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

### 1 — Broadcasting

```cpp
// Wrap any USTRUCT into FInstancedStruct and broadcast.
FMyCustomMessage Msg;
Msg.SomeField = 42;

FInstancedStruct Payload = FInstancedStruct::Make(Msg);

if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
{
    Bus->Broadcast(
        MyTags::SomeChannel,
        MoveTemp(Payload),
        EGameCoreEventScope::ServerOnly);
}
```

### 2 — Listening

```cpp
FGameplayMessageListenerHandle Handle;

void UMySystem::BeginPlay()
{
    Super::BeginPlay();

    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        Handle = Bus->StartListening(
            MyTags::SomeChannel,
            this,
            [this](FGameplayTag Channel, const FInstancedStruct& Message)
            {
                if (const FMyCustomMessage* Msg = Message.GetPtr<FMyCustomMessage>())
                {
                    // Use Msg->SomeField
                }
            });
    }
}

void UMySystem::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        Bus->StopListening(Handle);
    }
    Super::EndPlay(Reason);
}
```

### 3 — Dynamic Registration Pattern

```cpp
// Register handlers keyed by tag at runtime — the bus does not care about struct type.
TMap<FGameplayTag, FGameplayMessageListenerHandle> DynamicHandles;

void RegisterHandler(FGameplayTag Channel, TFunction<void(FGameplayTag, const FInstancedStruct&)> Fn)
{
    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        DynamicHandles.Add(Channel, Bus->StartListening(Channel, this, MoveTemp(Fn)));
    }
}
```

---

## Sub-Pages

| Sub-Page | Covers |
|---|---|
| [UGameCoreEventBus2](Event%20Bus%20System%202/UGameCoreEventBus2.md) | Class definition, all methods, implementation notes |
| [Scope Guard](Event%20Bus%20System%202/Scope%20Guard.md) | `EGameCoreEventScope` — shared with GMS1, documented here for GMS2 context |
| [Migration Guide](Event%20Bus%20System%202/Migration%20Guide.md) | How to migrate a GMS1 channel to GMS2; coexistence rules |
