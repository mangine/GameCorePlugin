# Event Bus System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The Event Bus System is a thin `UWorldSubsystem` wrapper around Unreal's `UGameplayMessageSubsystem` (GMS). It provides a single, cached broadcast point for all cross-system events emitted by GameCore modules, with a clear extension path for future features such as per-channel rate limiting and flush timers.

---

## Purpose

Delegates are the right tool for intra-system wiring — they are synchronous, typed, and zero-overhead when no listeners are bound. However, they force every external consumer to take a direct reference to the component that owns the delegate, creating coupling that undermines GameCore's module-independence guarantee.

`UGameCoreEventSubsystem` solves this by acting as a single, world-scoped broadcast bus. Any system can broadcast a typed message to a named channel. Any other system can listen to that channel with zero knowledge of the broadcaster.

---

## Design Rules

- **Delegates stay for intra-system wiring.** A component may still fire its own delegate and call `Broadcast` in the same mutation — the delegate is for systems inside the same module; GMS is for everything else.
- **One struct per event type.** Never share a payload struct between two unrelated events.
- **Channel tags follow `GameCoreEvent.<System>.<Event>`.** Defined in `DefaultGameplayTags.ini` inside the owning module. Never in a central tags file.
- **Server-only unless stated otherwise.** All GameCore events originate on the server. Client-side listeners must be explicitly documented per channel.
- **Never call `UGameplayMessageSubsystem` directly** from GameCore module code. Always go through `UGameCoreEventSubsystem::Broadcast`.

---

## File Location

```
GameCore/
└── Source/
    └── GameCore/
        └── EventBus/
            ├── GameCoreEventSubsystem.h / .cpp
            └── GameCoreEventMessages.h          ← all message structs
```

---

## `UGameCoreEventSubsystem`

```cpp
UCLASS()
class GAMECORE_API UGameCoreEventSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // Primary broadcast entry point for all GameCore modules.
    // Prefer this over calling GMS directly — future batching or rate-limiting
    // can be added here without touching call sites.
    template<typename T>
    void Broadcast(FGameplayTag Channel, const T& Message)
    {
        if (GMS)
        {
            GMS->BroadcastMessage(Channel, Message);
        }
    }

    // Registers a listener for a typed message on a channel.
    // Returns a handle that MUST be stored by the caller.
    // Call StopListening with the handle when the listener is no longer valid.
    template<typename T, typename TOwner>
    FGameplayMessageListenerHandle StartListening(
        FGameplayTag Channel,
        TOwner* Owner,
        void (TOwner::*Callback)(FGameplayTag, const T&))
    {
        if (GMS)
        {
            return GMS->RegisterListener<T>(Channel, Owner, Callback);
        }
        return FGameplayMessageListenerHandle{};
    }

    void StopListening(FGameplayMessageListenerHandle& Handle)
    {
        if (GMS && Handle.IsValid())
        {
            GMS->UnregisterListener(Handle);
            Handle = FGameplayMessageListenerHandle{};
        }
    }

private:
    // Cached at Initialize time. GMS is a UWorldSubsystem guaranteed to
    // outlive all other subsystems in the same world. Safe as a raw pointer.
    UPROPERTY()
    TObjectPtr<UGameplayMessageSubsystem> GMS;
};
```

### Initialize

```cpp
void UGameCoreEventSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Declare GMS as a dependency so the collection initializes it first.
    Collection.InitializeDependency<UGameplayMessageSubsystem>();
    GMS = GetWorld()->GetSubsystem<UGameplayMessageSubsystem>();
    check(GMS); // Always valid after InitializeDependency
}
```

> **Why `InitializeDependency`?** `UWorldSubsystem` initialization order is not guaranteed. Declaring GMS as a dependency ensures it is always initialized before `UGameCoreEventSubsystem::Initialize` runs — no null checks needed at broadcast sites.

---

## How to Use

### Broadcasting (inside a GameCore module)

```cpp
// Get the subsystem inline — it holds a cached GMS ptr, so cost is one pointer dereference.
if (UGameCoreEventSubsystem* EventBus = GetWorld()->GetSubsystem<UGameCoreEventSubsystem>())
{
    FProgressionLevelUpMessage Msg;
    Msg.PlayerState    = OwnerPS;
    Msg.ProgressionTag = ProgressionTag;
    Msg.OldLevel       = OldLevel;
    Msg.NewLevel       = NewLevel;
    EventBus->Broadcast(GameCoreEventTags::Progression_LevelUp, Msg);
}
```

> **Do not cache `UGameCoreEventSubsystem` itself as a member.** Subsystem lifetime is tied to the world; components may outlive a world transition. Call `GetWorld()->GetSubsystem<>()` inline — the actual GMS broadcast is already cached inside.

### Listening (from any system)

```cpp
FGameplayMessageListenerHandle LevelUpHandle;

void UMySystem::BeginPlay()
{
    if (UGameCoreEventSubsystem* EventBus = GetWorld()->GetSubsystem<UGameCoreEventSubsystem>())
    {
        LevelUpHandle = EventBus->StartListening<FProgressionLevelUpMessage>(
            GameCoreEventTags::Progression_LevelUp,
            this,
            &UMySystem::OnLevelUp);
    }
}

void UMySystem::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UGameCoreEventSubsystem* EventBus = GetWorld()->GetSubsystem<UGameCoreEventSubsystem>())
    {
        EventBus->StopListening(LevelUpHandle);
    }
}

void UMySystem::OnLevelUp(FGameplayTag Channel, const FProgressionLevelUpMessage& Message)
{
    // Message.PlayerState, Message.ProgressionTag, Message.OldLevel, Message.NewLevel
}
```

> **Always store the handle and call `StopListening` in `EndPlay`.** Leaked handles keep a dangling reference inside GMS.

---

## Module Dependency

```csharp
// In any module that broadcasts or listens:
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameCore",
    "GameplayTags",
    "GameplayMessageRuntime"  // for FGameplayMessageListenerHandle type visibility
});
```

---

## Channel Tag Conventions

All GameCore channels follow `GameCoreEvent.<System>.<Event>`. Defined per-module in `DefaultGameplayTags.ini`.

```
GameCoreEvent
  ├── Progression
  │     ├── LevelUp
  │     ├── XPChanged
  │     └── PointPoolChanged
  └── StateMachine
        ├── StateChanged
        └── TransitionBlocked
```

---

## Extension Path

When flush timers or per-channel rate limiting are needed, the implementation lives entirely inside `UGameCoreEventSubsystem::Broadcast`. Call sites do not change.

```cpp
// Future: per-channel buffered broadcast
template<typename T>
void Broadcast(FGameplayTag Channel, const T& Message)
{
    if (ShouldBuffer(Channel))
    {
        PendingMessages.Add({ Channel, MakeShared<T>(Message) });
        EnsureFlushTimer();
    }
    else
    {
        GMS->BroadcastMessage(Channel, Message);
    }
}
```

---

## Known Limitations

- **GMS is synchronous.** `BroadcastMessage` calls all listeners inline before returning. Heavy listener logic on a hot-path broadcast (e.g. `XPChanged`) should be deferred by the listener, not by the bus.
- **No cross-world channels.** Each `UGameCoreEventSubsystem` instance is world-scoped. Broadcasts do not cross world boundaries.
- **Blueprint support is limited.** The template API is C++ only. Blueprint systems that need to react to GameCore events should use thin Blueprint-callable wrappers in game-layer code.
