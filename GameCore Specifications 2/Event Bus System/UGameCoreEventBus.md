# UGameCoreEventBus

**File:** `GameCore/Source/GameCore/EventBus/GameCoreEventBus.h` / `.cpp`

`UGameCoreEventBus` is a `UWorldSubsystem` that wraps `UGameplayMessageSubsystem` (GMS) and acts as the **sole broadcast point** for all cross-system events in GameCore. It adds scope enforcement, payload validation, and a typed API on top of the raw GMS interface.

---

## `EGameCoreEventScope`

Defined in `GameCoreEventBus.h`. Controls which machine a broadcast is permitted to fire on.

```cpp
/**
 * Controls which machine(s) a broadcast is permitted to fire on.
 * The broadcaster decides scope — the bus enforces it.
 * This is NOT a replication mechanism. Scope never sends data over the network.
 */
UENUM(BlueprintType)
enum class EGameCoreEventScope : uint8
{
    /** Fires only with authority (server / standalone). Default. */
    ServerOnly,

    /** Fires only on the owning client or in standalone.
     *  Data must already exist on the client via replication before this broadcast fires. */
    ClientOnly,

    /** Fires on all machines.
     *  Use only when both sides independently need to react (e.g. AuthorityMode=Both component). */
    Both,
};
```

### Scope Evaluation

```
ServerOnly → NM_Client?                         → DROP
ClientOnly → not (NM_Client or NM_Standalone)?  → DROP
Both       →                                    → always PASS
```

`NM_Standalone` passes both `ServerOnly` and `ClientOnly` — standalone runs both roles.

---

## Class Declaration

```cpp
// GameCoreEventBus.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "GameplayMessageSubsystem.h"
#include "StructUtils/InstancedStruct.h"
#include "GameCoreEventBus.generated.h"

/**
 * UGameCoreEventBus
 *
 * FInstancedStruct-based event bus. Single broadcast point for all cross-system
 * events emitted by GameCore modules.
 *
 * Broadcast overloads:
 *   - Broadcast<T>   — typed; wraps T into FInstancedStruct automatically. Internal GameCore use only.
 *   - Broadcast      — raw FInstancedStruct; standard external call site pattern.
 *
 * StartListening overloads:
 *   - StartListening<T>  — typed; unwraps FInstancedStruct to T automatically.
 *   - StartListening     — raw FInstancedStruct; for callers needing runtime type inspection.
 *
 * TAG MATCHING IS EXACT. GMS does not support parent tag subscription.
 * Subscribing to GameCoreEvent.Combat will NOT receive events on
 * GameCoreEvent.Combat.EnemyKilled. Always subscribe to the specific leaf tag.
 *
 * HANDLE LIFETIME: StartListening callers are fully responsible for storing
 * the returned handle and calling StopListening in EndPlay. No automatic
 * cleanup is provided — leaked handles keep a dangling lambda alive in GMS.
 */
UCLASS()
class GAMECORE_API UGameCoreEventBus : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // -------------------------------------------------------------------------
    // Subsystem lifecycle
    // -------------------------------------------------------------------------

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // -------------------------------------------------------------------------
    // Static accessor
    // -------------------------------------------------------------------------

    /** Returns the bus for the world that owns WorldContext. Never null if the world is valid. */
    UFUNCTION(BlueprintCallable, Category="GameCore|EventBus", meta=(WorldContext="WorldContext"))
    static UGameCoreEventBus* Get(const UObject* WorldContext);

    // -------------------------------------------------------------------------
    // Broadcast — typed overload (internal GameCore systems only)
    // -------------------------------------------------------------------------

    /**
     * Broadcasts a strongly-typed struct by wrapping it in FInstancedStruct internally.
     * INTERNAL USE ONLY. External call sites must use the raw overload with FInstancedStruct::Make.
     */
    template<typename T>
    void Broadcast(
        FGameplayTag Channel,
        const T& Message,
        EGameCoreEventScope Scope = EGameCoreEventScope::ServerOnly);

    // -------------------------------------------------------------------------
    // Broadcast — raw FInstancedStruct overload (standard)
    // -------------------------------------------------------------------------

    /**
     * Broadcasts an FInstancedStruct payload on a GameplayTag channel.
     * Prefer MoveTemp on the caller side: Broadcast(Ch, MoveTemp(Msg), Scope).
     */
    void Broadcast(
        FGameplayTag Channel,
        FInstancedStruct Message,
        EGameCoreEventScope Scope = EGameCoreEventScope::ServerOnly);

    // -------------------------------------------------------------------------
    // StartListening — typed (preferred for single-channel listeners)
    // -------------------------------------------------------------------------

    /**
     * Registers a typed listener on a channel.
     * Internally unwraps FInstancedStruct → T via GetPtr<T>().
     * Type mismatch triggers an ensureMsgf in non-shipping builds and silently skips the callback.
     *
     * IMPORTANT: Tag matching is exact — child tags are NOT received.
     *
     * Caller is fully responsible for handle lifetime. Store the handle and
     * call StopListening in EndPlay — no automatic cleanup is provided.
     *
     * @return Handle. Store it and pass to StopListening in EndPlay.
     */
    template<typename T>
    FGameplayMessageListenerHandle StartListening(
        FGameplayTag Channel,
        TFunction<void(FGameplayTag, const T&)> Callback);

    // -------------------------------------------------------------------------
    // StartListening — raw (runtime type inspection)
    // -------------------------------------------------------------------------

    /**
     * Registers a raw listener. Use when struct type is only known at runtime.
     *
     * IMPORTANT: Tag matching is exact — child tags are NOT received.
     *
     * Caller is fully responsible for handle lifetime. Store the handle and
     * call StopListening in EndPlay — no automatic cleanup is provided.
     *
     * @return Handle. Store it and pass to StopListening in EndPlay.
     */
    FGameplayMessageListenerHandle StartListening(
        FGameplayTag Channel,
        TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback);

    /**
     * Unregisters a listener. Resets the handle to invalid.
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

## Template Implementations (in header, below class declaration)

```cpp
template<typename T>
void UGameCoreEventBus::Broadcast(
    FGameplayTag Channel,
    const T& Message,
    EGameCoreEventScope Scope)
{
    Broadcast(Channel, FInstancedStruct::Make(Message), Scope);
}

template<typename T>
FGameplayMessageListenerHandle UGameCoreEventBus::StartListening(
    FGameplayTag Channel,
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
                    TEXT("UGameCoreEventBus::StartListening<T> — type mismatch on channel %s. "
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

## `.cpp` Implementation

### `Initialize` / `Deinitialize`

```cpp
void UGameCoreEventBus::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UGameplayMessageSubsystem>();
    GMS = GetWorld()->GetSubsystem<UGameplayMessageSubsystem>();
    check(GMS);
}

void UGameCoreEventBus::Deinitialize()
{
    GMS = nullptr;
    Super::Deinitialize();
}
```

### `Get`

```cpp
UGameCoreEventBus* UGameCoreEventBus::Get(const UObject* WorldContext)
{
    if (!WorldContext) return nullptr;
    if (UWorld* World = GEngine->GetWorldFromContextObject(
            WorldContext, EGetWorldErrorMode::ReturnNull))
    {
        return World->GetSubsystem<UGameCoreEventBus>();
    }
    return nullptr;
}
```

> **Do not cache the subsystem pointer as a member field.** Always call `Get(this)` inline at call sites.

### `Broadcast` (raw overload)

```cpp
void UGameCoreEventBus::Broadcast(
    FGameplayTag Channel,
    FInstancedStruct Message,
    EGameCoreEventScope Scope)
{
    if (!PassesScopeGuard(Scope)) return;

    if (!ensureMsgf(Channel.IsValid(),
        TEXT("UGameCoreEventBus::Broadcast — invalid channel tag"))) return;

    if (!ensureMsgf(Message.IsValid(),
        TEXT("UGameCoreEventBus::Broadcast — empty FInstancedStruct on channel %s"),
        *Channel.ToString())) return;

    GMS->BroadcastMessage<FInstancedStruct>(Channel, Message);
}
```

### `StartListening` (raw overload)

```cpp
FGameplayMessageListenerHandle UGameCoreEventBus::StartListening(
    FGameplayTag Channel,
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

### `StopListening`

```cpp
void UGameCoreEventBus::StopListening(FGameplayMessageListenerHandle& Handle)
{
    if (GMS && Handle.IsValid())
    {
        GMS->UnregisterListener(Handle);
        Handle = FGameplayMessageListenerHandle{};
    }
}
```

### `PassesScopeGuard`

```cpp
bool UGameCoreEventBus::PassesScopeGuard(EGameCoreEventScope Scope) const
{
    const UWorld* World = GetWorld();
    if (!World) return false;

    switch (Scope)
    {
    case EGameCoreEventScope::ServerOnly:  return World->GetNetMode() != NM_Client;
    case EGameCoreEventScope::ClientOnly:  return World->GetNetMode() == NM_Client
                                               || World->GetNetMode() == NM_Standalone;
    case EGameCoreEventScope::Both:        return true;
    default:                               return false;
    }
}
```

---

## Important Notes

- **`T` must be a `USTRUCT`** for both `Broadcast<T>` and `StartListening<T>`.
- **Both templates live in the header** — template instantiation happens at the call site.
- **GMS is synchronous.** All listeners fire inline during `Broadcast`. Heavy work must be deferred by the listener (timer, game thread task).
- **Always store the handle and call `StopListening` in `EndPlay`.** Leaked handles keep a dangling lambda alive inside GMS indefinitely. The bus provides no automatic cleanup.
- **Tag matching is exact.** This is a GMS constraint, not a bus design gap.
