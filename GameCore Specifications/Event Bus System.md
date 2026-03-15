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
- **Channel tags follow `GameCoreEvent.<s>.<Event>`.** Defined in `DefaultGameplayTags.ini` inside the owning module. Never in a central tags file.
- **Every channel must declare its scope.** The broadcaster decides scope — the bus enforces it. Document the scope in the channel's entry in `GameCore Event Messages.md`. Default is `ServerOnly`.
- **Never call `UGameplayMessageSubsystem` directly** from GameCore module code. Always go through `UGameCoreEventSubsystem::Broadcast`.
- **`ClientOnly` requires prior replication.** A client-side GMS broadcast is a notification layer only — the data it references must already exist on the client via `FFastArraySerializer` or a replicated property before the broadcast fires.
- **Do not cache `UGameCoreEventSubsystem` as a member.** Subsystem lifetime is tied to the world; components may outlive a world transition. Use `UGameCoreEventSubsystem::Get(this)` inline at call sites.

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

## Broadcast Scope

```cpp
/** Controls which machine(s) the GMS broadcast fires on.
 *  The broadcaster (e.g. ULevelingComponent) decides scope — the bus enforces it.
 *  This is not a replication mechanism; it is a guard against firing on the wrong machine.
 */
UENUM(BlueprintType)
enum class EGameCoreEventScope : uint8
{
    /** Broadcast only when running with authority (server / standalone).
     *  Use for all events that originate from server-side logic. Default. */
    ServerOnly  UMETA(DisplayName = "Server Only"),

    /** Broadcast only on a net client (NM_Client).
     *  Use when data has already been replicated and the client fires its own notification.
     *  No-op when called on the server. */
    ClientOnly  UMETA(DisplayName = "Client Only"),

    /** Broadcast unconditionally on whatever machine calls it.
     *  Use only when the caller has already determined the correct machine context
     *  (e.g. Both-authority state machine components). */
    Both        UMETA(DisplayName = "Both"),
};
```

> **Scope is a call-site guard, not a replication primitive.** It prevents accidental double-fires and wrong-machine broadcasts. It does not send data across the network.

---

## `UGameCoreEventSubsystem`

```cpp
UCLASS()
class GAMECORE_API UGameCoreEventSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** Convenience accessor. Prefer over GetWorld()->GetSubsystem<>() at call sites.
     *  Returns nullptr if WorldContext has no valid world. */
    static UGameCoreEventSubsystem* Get(const UObject* WorldContext);

    /** Primary broadcast entry point for all GameCore modules.
     *  Scope controls which machine the broadcast fires on (default: ServerOnly).
     *  Future batching or rate-limiting can be added here without touching call sites. */
    template<typename T>
    void Broadcast(FGameplayTag Channel, const T& Message, EGameCoreEventScope Scope = EGameCoreEventScope::ServerOnly)
    {
        if (!GMS) return;

        switch (Scope)
        {
            case EGameCoreEventScope::ServerOnly:
                if (GetWorld()->GetNetMode() < NM_Client)
                    GMS->BroadcastMessage(Channel, Message);
                break;

            case EGameCoreEventScope::ClientOnly:
                if (GetWorld()->GetNetMode() == NM_Client)
                    GMS->BroadcastMessage(Channel, Message);
                break;

            case EGameCoreEventScope::Both:
                GMS->BroadcastMessage(Channel, Message);
                break;
        }
    }

    /** Registers a listener for a typed message on a channel.
     *  Returns a handle that MUST be stored by the caller.
     *  Call StopListening with the handle when the listener is no longer valid. */
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

### Initialize & Get

```cpp
void UGameCoreEventSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Declare GMS as a dependency so the collection initializes it first.
    Collection.InitializeDependency<UGameplayMessageSubsystem>();
    GMS = GetWorld()->GetSubsystem<UGameplayMessageSubsystem>();
    check(GMS); // Always valid after InitializeDependency
}

UGameCoreEventSubsystem* UGameCoreEventSubsystem::Get(const UObject* WorldContext)
{
    if (!WorldContext) return nullptr;
    if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull))
        return World->GetSubsystem<UGameCoreEventSubsystem>();
    return nullptr;
}
```

> **Why `InitializeDependency`?** `UWorldSubsystem` initialization order is not guaranteed. Declaring GMS as a dependency ensures it is always initialized before `UGameCoreEventSubsystem::Initialize` runs — no null checks needed at broadcast sites.

---

## How to Use

### Broadcasting (inside a GameCore module)

```cpp
// Use Get() instead of GetWorld()->GetSubsystem<>().
if (UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this))
{
    FProgressionLevelUpMessage Msg;
    Msg.PlayerState    = OwnerPS;
    Msg.ProgressionTag = ProgressionTag;
    Msg.OldLevel       = OldLevel;
    Msg.NewLevel       = NewLevel;

    // LevelUp fires server-side only (default). The client reacts via replicated data.
    Bus->Broadcast(GameCoreEventTags::Progression_LevelUp, Msg, EGameCoreEventScope::ServerOnly);
}
```

### Listening (from any system)

```cpp
FGameplayMessageListenerHandle LevelUpHandle;

void UMySystem::BeginPlay()
{
    if (UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this))
    {
        LevelUpHandle = Bus->StartListening<FProgressionLevelUpMessage>(
            GameCoreEventTags::Progression_LevelUp,
            this,
            &UMySystem::OnLevelUp);
    }
}

void UMySystem::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this))
    {
        Bus->StopListening(LevelUpHandle);
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

All GameCore channels follow `GameCoreEvent.<s>.<Event>`. Defined per-module in `DefaultGameplayTags.ini`.

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

## Extension Path

When flush timers or per-channel rate limiting are needed, the implementation lives entirely inside `UGameCoreEventSubsystem::Broadcast`. Call sites do not change.

```cpp
// Future: per-channel buffered broadcast
template<typename T>
void Broadcast(FGameplayTag Channel, const T& Message, EGameCoreEventScope Scope = EGameCoreEventScope::ServerOnly)
{
    if (!PassesScopeGuard(Scope)) return;

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
- **`XPChanged` and `PointPoolChanged` can be high-frequency.** If fired per individual XP grant during combat, batching must be applied at the call site (accumulate server-side, broadcast once per frame or threshold). The bus does not throttle.
- **Scope is not a replication primitive.** `ClientOnly` events require data to already be present on the client via replication before the broadcast fires.
- **No cross-world channels.** Each `UGameCoreEventSubsystem` instance is world-scoped. Broadcasts do not cross world boundaries.
- **Blueprint support is limited.** The template API is C++ only. Blueprint systems that need to react to GameCore events should use thin Blueprint-callable wrappers in game-layer code.
