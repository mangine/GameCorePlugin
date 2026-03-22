// PersistenceSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "Persistence/PersistenceTypes.h"
#include "PersistenceSubsystem.generated.h"

class UPersistenceRegistrationComponent;

/**
 * Central coordinator of the persistence system.
 * Owns all timing, actor registries, the dirty set, and the load callback map.
 * Produces FEntityPersistencePayload blobs and routes them via FGameplayTag-keyed delegates.
 * No knowledge of storage backends — transport is external.
 *
 * Server-only. ShouldCreateSubsystem returns false on clients.
 */
UCLASS(Config=Game)
class GAMECORE_API UPersistenceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // --- Configuration (set via DefaultGame.ini) ---

    /** Seconds between save cycle ticks. Default 300s (5 min). */
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Timing")
    float SaveInterval = 300.f;

    /**
     * Number of partial cycles before a full cycle.
     * 0 = every cycle is Full. 9 = 9 partials then 1 full (default).
     */
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Timing")
    int32 PartialSavesBetweenFullSave = 9;

    /** Actors serialized per game tick. Applies to both partial and full cycles. */
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Performance")
    int32 ActorsPerFlushTick = 100;

    /** Seconds before a pending load callback fires OnComplete(false). */
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Load")
    float LoadTimeoutSeconds = 30.f;

    /**
     * Stable server identity stamped into every FEntityPersistencePayload.
     * Must be configured before Initialize() via DefaultGame.ini or UGameInstance override.
     * Invalid GUID breaks cross-restart deduplication in audit tooling.
     */
    UPROPERTY(Config, EditDefaultsOnly, Category="Persistence|Identity")
    FGuid ServerInstanceId;

    // --- Tag Delegate API ---

    DECLARE_MULTICAST_DELEGATE_OneParam(FOnPayloadReady, const FEntityPersistencePayload&);

    /** Register a tag so the subsystem can route payloads to it. Must be called before BeginPlay of any actor using this tag. */
    void RegisterPersistenceTag(FGameplayTag Tag);

    /** Returns the delegate for a tag. Bind your transport handler here. Returns null if tag not registered. */
    FOnPayloadReady* GetSaveDelegate(FGameplayTag Tag);

    // --- Load API ---

    /**
     * Fired when a load is requested. Game module transport should listen here
     * and fetch from DB, then call OnRawPayloadReceived or OnLoadFailed.
     * Only one transport should bind per tag to avoid duplicate fetch attempts.
     */
    DECLARE_MULTICAST_DELEGATE_TwoParams(FOnLoadRequested, FGuid /*EntityId*/, FGameplayTag /*Tag*/);
    FOnLoadRequested OnLoadRequested;

    /**
     * Request deserialization of an actor from persistent storage.
     * OnComplete fires true on success, false on failure or timeout.
     * Actor must already exist in the world before calling this.
     */
    void RequestLoad(FGuid EntityId, FGameplayTag Tag,
        TFunction<void(bool bSuccess)> OnComplete);

    /**
     * Called by game module transport when DB returns payload data.
     * Deserializes blobs into components, calls Migrate() on version mismatch, fires OnComplete.
     */
    void OnRawPayloadReceived(AActor* Actor, const FEntityPersistencePayload& Payload);

    /** Called by transport on DB fetch failure. Fires OnComplete(false) and removes the pending request. */
    void OnLoadFailed(FGuid EntityId);

    // --- Event-Driven Save API ---

    /**
     * Force an immediate full save for a single actor.
     * Sets bCritical and bFlushImmediately based on ESerializationReason.
     * Removes actor from DirtySet after dispatch.
     */
    void RequestFullSave(AActor* Entity, ESerializationReason Reason);

    /**
     * Synchronous shutdown save. Cancels all timers, serializes all registered entities.
     * All payloads stamped bCritical=true, bFlushImmediately=true.
     * Safe only because RegisteredEntities is bounded by design (no NPCs/Mobs by default).
     */
    void RequestShutdownSave();

    // --- Internal API (called by UPersistenceRegistrationComponent) ---

    void RegisterEntity(UPersistenceRegistrationComponent* RegComp);
    void UnregisterEntity(FGuid EntityId);
    void EnqueueDirty(FGuid EntityId);

protected:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

private:
    // --- Registries ---
    TMap<FGuid, TWeakObjectPtr<UPersistenceRegistrationComponent>> RegisteredEntities;
    TSet<FGuid> DirtySet;
    TMap<FGameplayTag, FOnPayloadReady> TagDelegates;

    // --- Load Callbacks ---
    struct FLoadRequest
    {
        TFunction<void(bool)> Callback;
        float Timestamp; // FPlatformTime::Seconds() at enqueue time
    };
    TMap<FGuid, FLoadRequest> LoadCallbacks;

    // --- Cycle State ---
    uint32 SaveCounter = 0;

    bool          bFullCycleInProgress = false;
    int32         FullCycleCursorIndex = 0;
    TArray<FGuid> FullCycleEntitySnapshot;

    // --- Timers ---
    FTimerHandle SaveTimer;
    FTimerHandle FullCycleTickTimer;
    FTimerHandle LoadTimeoutTimer;

    // --- Internal Methods ---
    void FlushSaveCycle();
    void FlushPartialCycle();
    void TickFullCycle();
    void DispatchPayload(const FEntityPersistencePayload& Payload);
    void TickLoadTimeouts();

    static bool IsCriticalReason(ESerializationReason Reason);
    static bool IsImmediateReason(ESerializationReason Reason);
};
