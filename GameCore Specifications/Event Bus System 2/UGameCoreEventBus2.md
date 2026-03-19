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
     *
     * The scope guard is evaluated before forwarding to GMS.
     * If the guard fails (wrong machine), the broadcast is silently dropped.
     */
    void Broadcast(
        FGameplayTag Channel,
        FInstancedStruct Message,
        EGameCoreEventScope Scope = EGameCoreEventScope::ServerOnly);

    // -------------------------------------------------------------------------
    // Listen / Unlisten
    // -------------------------------------------------------------------------

    /**
     * Register a listener on a channel.
     *
     * @param Channel   The channel tag to listen on.
     * @param Owner     The owning UObject. Used only for lifetime bookkeeping by the caller;
     *                  GMS2 does not dereference this pointer internally.
     * @param Callback  TFunction receiving (Channel, const FInstancedStruct&).
     *                  Use GetPtr<T>() inside the callback to unwrap the payload.
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

    /** Evaluates the scope guard. Returns true if the broadcast should proceed. */
    bool PassesScopeGuard(EGameCoreEventScope Scope) const;

    /** Cached at Initialize time. GMS is a UWorldSubsystem guaranteed to outlive
     *  all other subsystems in the same world. Raw pointer is safe. */
    UPROPERTY()
    TObjectPtr<UGameplayMessageSubsystem> GMS;
};
```

---

## Implementation

### Initialize / Deinitialize

```cpp
void UGameCoreEventBus2::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Declare GMS as a dependency so the collection initializes it first.
    Collection.InitializeDependency<UGameplayMessageSubsystem>();
    GMS = GetWorld()->GetSubsystem<UGameplayMessageSubsystem>();
    check(GMS); // Always valid after InitializeDependency
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

> **Do not cache the subsystem pointer as a member in other classes.** Component lifetime can outlive world transitions. Always call `Get(this)` inline.

### Broadcast

```cpp
void UGameCoreEventBus2::Broadcast(
    FGameplayTag Channel,
    FInstancedStruct Message,
    EGameCoreEventScope Scope)
{
    if (!PassesScopeGuard(Scope)) return;

    if (!ensureMsgf(Channel.IsValid(), TEXT("UGameCoreEventBus2::Broadcast — invalid channel tag")))
        return;

    if (!ensureMsgf(Message.IsValid(), TEXT("UGameCoreEventBus2::Broadcast — empty FInstancedStruct on channel %s"),
        *Channel.ToString()))
        return;

    GMS->BroadcastMessage<FInstancedStruct>(Channel, Message);
}
```

**Notes:**
- `Message` is taken by value so the caller can `MoveTemp` to avoid a copy.
- An empty `FInstancedStruct` (default-constructed, no inner type) is a programmer error — the `ensureMsgf` fires in non-shipping builds.
- GMS copies the struct internally before returning; `Message` can be destroyed after `Broadcast` returns.

### StartListening

```cpp
FGameplayMessageListenerHandle UGameCoreEventBus2::StartListening(
    FGameplayTag Channel,
    UObject* Owner,
    TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback)
{
    if (!GMS || !Channel.IsValid() || !Callback) return FGameplayMessageListenerHandle{};

    // GMS::RegisterListener requires a member function pointer.
    // We bridge TFunction via a UObject trampoline wrapper.
    // The simplest bridge: use the lambda-capturing overload of RegisterListener.
    return GMS->RegisterListener<FInstancedStruct>(
        Channel,
        [CB = MoveTemp(Callback)](FGameplayTag InChannel, const FInstancedStruct& InMsg)
        {
            CB(InChannel, InMsg);
        });
}
```

> **GMS `RegisterListener` lambda overload:** `UGameplayMessageSubsystem::RegisterListener<T>` has an overload accepting a `TFunction<void(FGameplayTag, const T&)>`. This is the overload used here — no member function pointer required.

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
    case EGameCoreEventScope::ServerOnly:
        return World->GetNetMode() != NM_Client;

    case EGameCoreEventScope::ClientOnly:
        return World->GetNetMode() == NM_Client;

    case EGameCoreEventScope::Both:
        return true;

    default:
        return false;
    }
}
```

---

## Important Notes

- **GMS is synchronous.** `BroadcastMessage` invokes all listeners inline before returning. Listeners with heavy logic should defer their work (e.g. queue a tick task) rather than processing inline.
- **`FInstancedStruct` owns its memory.** Each `BroadcastMessage` call causes GMS to copy the struct into its internal storage. The `FInstancedStruct` passed to `Broadcast` can be stack-allocated — no heap management needed at call sites.
- **Type safety is the listener's responsibility.** GMS2 carries no compile-time type constraint per channel. Mismatches between broadcaster struct type and `GetPtr<T>()` in the listener return `nullptr` — handle gracefully.
- **Always store the handle and call `StopListening` in `EndPlay`.** Leaked handles keep a dangling lambda inside GMS.
- **`Owner` parameter is not dereferenced by the bus.** It is a caller-side bookkeeping hint only. The bus does not hold a reference to it. If you need automatic unregistration tied to owner lifetime, call `StopListening` in `EndPlay` manually — there is no auto-cleanup.
