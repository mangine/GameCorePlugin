#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "GameplayMessageSubsystem.h"
#include "StructUtils/InstancedStruct.h"
#include "GameCoreEventBus.generated.h"

/**
 * Record of one currently-active event in the shared Active Event Registry.
 * Registered by any system; queried by any system without coupling.
 */
USTRUCT()
struct FActiveEventRecord
{
    GENERATED_BODY()

    FGuid        EventId;
    FGameplayTag EventTag;
    double       RegisteredAtSeconds = 0.0;  // FPlatformTime::Seconds() at registration
    float        ExpectedDuration    = 0.f;  // 0 = indefinite
    TWeakObjectPtr<UObject> Instigator;
};

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

    // -------------------------------------------------------------------------
    // Active Event Registry
    // -------------------------------------------------------------------------

    /**
     * Register an active event into the global registry.
     * Tag hierarchy is indexed — querying a parent tag matches all children.
     * @param EventTag   Tag identifying the event.
     * @param Duration   Expected duration in seconds. 0 = indefinite (must be manually unregistered).
     * @param Instigator Optional UObject for debug attribution.
     * @return FGuid handle for unregistration.
     */
    FGuid RegisterActiveEvent(
        FGameplayTag EventTag,
        float        Duration,
        UObject*     Instigator = nullptr);

    /** Unregister an active event. Safe with invalid or already-removed GUID. */
    void UnregisterActiveEvent(FGuid EventId);

    /** True if any event matching EventTag (or a child tag) is currently active. */
    bool IsEventActive(FGameplayTag EventTag) const;

    /** All active event GUIDs whose tags match EventTag or its children. */
    TArray<FGuid> GetActiveEvents(FGameplayTag CategoryTag) const;

    /** All active event records. For debug/tooling use. */
    const TMap<FGuid, FActiveEventRecord>& GetAllActiveEvents() const { return ActiveEventRegistry; }

private:

    bool PassesScopeGuard(EGameCoreEventScope Scope) const;
    void SweepExpiredEvents();

    UPROPERTY()
    TObjectPtr<UGameplayMessageSubsystem> GMS;

    TMap<FGuid, FActiveEventRecord> ActiveEventRegistry;
    FTimerHandle ExpiryTimerHandle;
};

// =============================================================================
// Template implementations
// =============================================================================

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
