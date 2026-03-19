# UGameCoreEventBus2

**Sub-page of:** [Event Bus System 2](../Event%20Bus%20System%202.md)

---

## Class Declaration

```cpp
// GameCoreEventBus2.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "GameplayMessageSubsystem.h"
#include "StructUtils/InstancedStruct.h"
#include "GameCoreEventSubsystem.h"   // for EGameCoreEventScope
#include "GameCoreEventBus2.generated.h"

/**
 * UGameCoreEventBus2
 *
 * FInstancedStruct-based event bus. Identical contract to UGameCoreEventSubsystem (GMS1)
 * but the payload is always FInstancedStruct, enabling runtime-dynamic message types.
 *
 * Two StartListening overloads are provided:
 *   - StartListening<T>  — typed; unwraps FInstancedStruct to T automatically.
 *   - StartListening     — raw FInstancedStruct; for callers that need to inspect type at runtime.
 *
 * Both subsystems coexist. Do not remove GMS1 until migration is complete.
 */
UCLASS()
class GAMECORE_API UGameCoreEventBus2 : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // -------------------------------------------------------------------------
    // Subsystem lifecycle
    // -------------------------------------------------------------------------

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // -------------------------------------------------------------------------
    // Static accessor — use instead of GetWorld()->GetSubsystem<>()
    // -------------------------------------------------------------------------

    /** Returns the bus for the world that owns WorldContext. Never null if the world is valid. */
    UFUNCTION(BlueprintCallable, Category="GameCore|EventBus2", meta=(WorldContext="WorldContext"))
    static UGameCoreEventBus2* Get(const UObject* WorldContext);

    // -------------------------------------------------------------------------
    // Broadcast
    // -------------------------------------------------------------------------

    /**
     * Broadcast an FInstancedStruct payload on a GameplayTag channel.
     *
     * @param Channel   The channel tag. Must be a valid leaf tag.
     * @param Message   The wrapped struct. Passed by value — caller should MoveTemp.
     * @param Scope     Scope guard. Default: ServerOnly.
     */
    void Broadcast(
        FGameplayTag Channel,
        FInstancedStruct Message,
        EGameCoreEventScope Scope = EGameCoreEventScope::ServerOnly);

    // -------------------------------------------------------------------------
    // Listen / Unlisten — typed overload (preferred)
    // -------------------------------------------------------------------------

    /**
     * Register a typed listener on a channel.
     *
     * The bus unwraps FInstancedStruct to T internally via GetPtr<T>().
     * If the inner struct type does not match T, the callback is silently skipped.
     * This is the preferred overload — callers never touch FInstancedStruct directly.
     *
     * @param Channel   The channel tag to listen on.
     * @param Owner     Caller-side bookkeeping only; not dereferenced by the bus.
     * @param Callback  TFunction receiving (Channel, const T&).
     *
     * @return A handle. Store it — pass it to StopListening in EndPlay.
     */
    template<typename T>
    FGameplayMessageListenerHandle StartListening(
        FGameplayTag Channel,
        UObject* Owner,
        TFunction<void(FGameplayTag, const T&)> Callback);

    // -------------------------------------------------------------------------
    // Listen / Unlisten — raw FInstancedStruct overload
    // -------------------------------------------------------------------------

    /**
     * Register a raw listener on a channel.
     *
     * Use this overload when the message type is not known at compile time
     * and the caller needs to inspect the inner struct type at runtime
     * via Message.GetScriptStruct() or Message.GetPtr<T>() manually.
     *
     * @param Channel   The channel tag to listen on.
     * @param Owner     Caller-side bookkeeping only; not dereferenced by the bus.
     * @param Callback  TFunction receiving (Channel, const FInstancedStruct&).
     *
     * @return A handle. Store it — pass it to StopListening in EndPlay.
     */
    FGameplayMessageListenerHandle StartListening(
        FGameplayTag Channel,
        UObject* Owner,
        TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback);

    /**
     * Unregister a listener. Resets the handle to invalid after unregistration.
     * Safe to call with an already-invalid handle.
     */
    void StopListening(FGameplayMessageListenerHandle& Handle);

private:

    bool PassesScopeGuard(EGameCoreEventScope Scope) const;

    UPROPERTY()
    TObjectPtr<UGameplayMessageSubsystem> GMS;
};
```

---

## Template Implementation (must live in the header)

```cpp
// GameCoreEventBus2.h — below the class declaration

template<typename T>
FGameplayMessageListenerHandle UGameCoreEventBus2::StartListening(
    FGameplayTag Channel,
    UObject* Owner,
    TFunction<void(FGameplayTag, const T&)> Callback)
{
    if (!GMS || !Channel.IsValid() || !Callback) return FGameplayMessageListenerHandle{};

    // Register on GMS as FInstancedStruct. On each broadcast, unwrap to T.
    // If the inner type does not match T, GetPtr<T>() returns nullptr — skip silently.
    return GMS->RegisterListener<FInstancedStruct>(
        Channel,
        [CB = MoveTemp(Callback)](FGameplayTag InChannel, const FInstancedStruct& InMsg)
        {
            if (const T* TypedMsg = InMsg.GetPtr<T>())
            {
                CB(InChannel, *TypedMsg);
            }
            // Type mismatch: silently skipped. This is a programmer error
            // (wrong struct type on channel) caught during development via ensureMsgf below.
#if !UE_BUILD_SHIPPING
            else
            {
                ensureMsgf(false,
                    TEXT("UGameCoreEventBus2::StartListening<T> — type mismatch on channel %s. "
                         "Expected %s, got %s."),
                    *InChannel.ToString(),
                    *T::StaticStruct()->GetName(),
                    InMsg.IsValid() ? *InMsg.GetScriptStruct()->GetName() : TEXT("(empty)"));
            }
#endif
        });
}
```

**Notes:**
- The template must be defined in the `.h` file, not the `.cpp`, because it is instantiated at the call site.
- The `ensureMsgf` in non-shipping builds surfaces type mismatches immediately during development. In shipping it is compiled out — the mismatch is a silent skip with zero overhead.
- `T::StaticStruct()` requires `T` to be a `USTRUCT`. This is always true for valid GMS2 message types.

---

## Non-Template Implementation (.cpp)

### Initialize / Deinitialize

```cpp
void UGameCoreEventBus2::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UGameplayMessageSubsystem>();
    GMS = GetWorld()->GetSubsystem<UGameplayMessageSubsystem>();
    check(GMS);
}

void UGameCoreEventBus2::Deinitialize()
{
    GMS = nullptr;
    Super::Deinitialize();
}
```

### Get

```cpp
UGameCoreEventBus2* UGameCoreEventBus2::Get(const UObject* WorldContext)
{
    if (!WorldContext) return nullptr;
    if (UWorld* World = GEngine->GetWorldFromContextObject(
            WorldContext, EGetWorldErrorMode::ReturnNull))
    {
        return World->GetSubsystem<UGameCoreEventBus2>();
    }
    return nullptr;
}
```

> **Do not cache the subsystem pointer as a member in other classes.** Always call `Get(this)` inline.

### Broadcast

```cpp
void UGameCoreEventBus2::Broadcast(
    FGameplayTag Channel,
    FInstancedStruct Message,
    EGameCoreEventScope Scope)
{
    if (!PassesScopeGuard(Scope)) return;

    if (!ensureMsgf(Channel.IsValid(),
        TEXT("UGameCoreEventBus2::Broadcast — invalid channel tag"))) return;

    if (!ensureMsgf(Message.IsValid(),
        TEXT("UGameCoreEventBus2::Broadcast — empty FInstancedStruct on channel %s"),
        *Channel.ToString())) return;

    GMS->BroadcastMessage<FInstancedStruct>(Channel, Message);
}
```

### StartListening (raw overload)

```cpp
FGameplayMessageListenerHandle UGameCoreEventBus2::StartListening(
    FGameplayTag Channel,
    UObject* Owner,
    TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback)
{
    if (!GMS || !Channel.IsValid() || !Callback) return FGameplayMessageListenerHandle{};

    return GMS->RegisterListener<FInstancedStruct>(
        Channel,
        [CB = MoveTemp(Callback)](FGameplayTag InChannel, const FInstancedStruct& InMsg)
        {
            CB(InChannel, InMsg);
        });
}
```

### StopListening

```cpp
void UGameCoreEventBus2::StopListening(FGameplayMessageListenerHandle& Handle)
{
    if (GMS && Handle.IsValid())
    {
        GMS->UnregisterListener(Handle);
        Handle = FGameplayMessageListenerHandle{};
    }
}
```

### PassesScopeGuard

```cpp
bool UGameCoreEventBus2::PassesScopeGuard(EGameCoreEventScope Scope) const
{
    const UWorld* World = GetWorld();
    if (!World) return false;

    switch (Scope)
    {
    case EGameCoreEventScope::ServerOnly:  return World->GetNetMode() != NM_Client;
    case EGameCoreEventScope::ClientOnly:  return World->GetNetMode() == NM_Client;
    case EGameCoreEventScope::Both:        return true;
    default:                               return false;
    }
}
```

---

## Usage Examples

### Typed overload (preferred)

```cpp
FGameplayMessageListenerHandle LevelUpHandle;

void UMySystem::BeginPlay()
{
    Super::BeginPlay();

    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        LevelUpHandle = Bus->StartListening<FProgressionLevelUpMessage>(
            GameCoreEventTags::Progression_LevelUp,
            this,
            [this](FGameplayTag Channel, const FProgressionLevelUpMessage& Msg)
            {
                // Msg is already unwrapped — no GetPtr needed.
                UE_LOG(LogTemp, Log, TEXT("Level up to %d"), Msg.NewLevel);
            });
    }
}

void UMySystem::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UGameCoreEventBus2* Bus = UGameCoreEventBus2::Get(this))
    {
        Bus->StopListening(LevelUpHandle);
    }
    Super::EndPlay(Reason);
}
```

### Raw overload (runtime type inspection)

```cpp
Handle = Bus->StartListening(
    MyTags::SomeChannel,
    this,
    [](FGameplayTag Channel, const FInstancedStruct& Message)
    {
        // Inspect type at runtime.
        if (const FMyTypeA* A = Message.GetPtr<FMyTypeA>()) { /* ... */ }
        else if (const FMyTypeB* B = Message.GetPtr<FMyTypeB>()) { /* ... */ }
    });
```

---

## Important Notes

- **GMS is synchronous.** All listeners are invoked inline during `Broadcast`. Heavy work must be deferred by the listener.
- **Template lives in the header.** The typed `StartListening<T>` must be defined in `GameCoreEventBus2.h` — not the `.cpp` — due to C++ template instantiation rules.
- **Type mismatch fires `ensureMsgf` in non-shipping builds.** In shipping, mismatches are a silent skip. Design channels so only one struct type is ever broadcast on a given tag.
- **`T` must be a `USTRUCT`.** `T::StaticStruct()` is called inside the template. Non-USTRUCT types will not compile.
- **Always store the handle and call `StopListening` in `EndPlay`.** Leaked handles keep a dangling lambda inside GMS.
