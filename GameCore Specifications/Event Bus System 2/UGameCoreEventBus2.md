# UGameCoreEventBus2

**Sub-page of:** [Event Bus System](../Event%20Bus%20System%202.md)

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
#include "GameCoreEventBus2.generated.h"

/**
 * UGameCoreEventBus2
 *
 * FInstancedStruct-based event bus. Single broadcast point for all cross-system
 * events emitted by GameCore modules.
 *
 * Broadcast overloads:
 *   - Broadcast<T>   — typed; wraps T into FInstancedStruct automatically. Internal use only.
 *   - Broadcast      — raw FInstancedStruct; standard external call site pattern.
 *
 * StartListening overloads:
 *   - StartListening<T>  — typed; unwraps FInstancedStruct to T automatically.
 *   - StartListening     — raw FInstancedStruct; for callers that need runtime type inspection.
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
    UFUNCTION(BlueprintCallable, Category="GameCore|EventBus", meta=(WorldContext="WorldContext"))
    static UGameCoreEventBus2* Get(const UObject* WorldContext);

    // -------------------------------------------------------------------------
    // Broadcast — typed overload (internal systems only)
    // -------------------------------------------------------------------------

    /**
     * Broadcast a strongly-typed struct directly, without manually wrapping it in FInstancedStruct.
     *
     * NOTE: This overload is intended for internal GameCore systems only (e.g. UStatComponent).
     * External call sites should construct FInstancedStruct::Make(Msg) explicitly and call
     * the raw Broadcast overload — this keeps broadcast sites readable and intentional.
     */
    template<typename T>
    void Broadcast(
        FGameplayTag Channel,
        const T& Message,
        EGameCoreEventScope Scope = EGameCoreEventScope::ServerOnly);

    // -------------------------------------------------------------------------
    // Broadcast — raw FInstancedStruct overload
    // -------------------------------------------------------------------------

    /**
     * Broadcast an FInstancedStruct payload on a GameplayTag channel.
     * Caller should MoveTemp the payload in.
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
     * The bus unwraps FInstancedStruct to T internally via GetPtr<T>().
     * If the inner struct type does not match T, the callback is silently skipped
     * and an ensure fires in non-shipping builds.
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
     * Register a raw listener. Use when the message type is only known at runtime.
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

## Template Implementations (must live in the header)

```cpp
// GameCoreEventBus2.h — below the class declaration

template<typename T>
void UGameCoreEventBus2::Broadcast(
    FGameplayTag Channel,
    const T& Message,
    EGameCoreEventScope Scope)
{
    Broadcast(Channel, FInstancedStruct::Make(Message), Scope);
}

template<typename T>
FGameplayMessageListenerHandle UGameCoreEventBus2::StartListening(
    FGameplayTag Channel,
    UObject* Owner,
    TFunction<void(FGameplayTag, const T&)> Callback)
{
    if (!GMS || !Channel.IsValid() || !Callback) return FGameplayMessageListenerHandle{};

    return GMS->RegisterListener<FInstancedStruct>(
        Channel,
        [CB = MoveTemp(Callback)](FGameplayTag InChannel, const FInstancedStruct& InMsg)
        {
            if (const T* TypedMsg = InMsg.GetPtr<T>())
            {
                CB(InChannel, *TypedMsg);
            }
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

### Broadcast (raw overload)

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

### Broadcast\<T\> — internal systems

```cpp
FStatChangedEvent Event;
Event.StatTag  = StatTag;
Event.NewValue = Value;
Event.Delta    = Delta;

Bus->Broadcast<FStatChangedEvent>(TAG_Event_StatChanged, Event, EGameCoreEventScope::ServerOnly);
```

### Broadcast (raw) — external call sites

```cpp
FMyMessage Msg;
Msg.SomeField = 42;

Bus->Broadcast(MyTags::SomeChannel, FInstancedStruct::Make(Msg), EGameCoreEventScope::ServerOnly);
```

### StartListening\<T\> — typed listener (preferred)

```cpp
LevelUpHandle = Bus->StartListening<FProgressionLevelUpMessage>(
    GameCoreEventTags::Progression_LevelUp,
    this,
    [this](FGameplayTag, const FProgressionLevelUpMessage& Msg)
    {
        UE_LOG(LogTemp, Log, TEXT("Level up to %d"), Msg.NewLevel);
    });
```

### StartListening (raw) — runtime type inspection

```cpp
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

## Important Notes

- **`Broadcast<T>` is for internal GameCore systems only.** External call sites use the raw overload with `FInstancedStruct::Make`.
- **GMS is synchronous.** All listeners are invoked inline during `Broadcast`. Heavy work must be deferred by the listener.
- **Both templates live in the header.** Template instantiation happens at the call site.
- **`T` must be a `USTRUCT`** for both `Broadcast<T>` and `StartListening<T>`.
- **Always store the handle and call `StopListening` in `EndPlay`.** Leaked handles keep a dangling lambda inside GMS.
