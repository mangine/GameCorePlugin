// PersistenceRegistrationComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Persistence/PersistenceTypes.h"
#include "Persistence/PersistableComponent.h"
#include "PersistenceRegistrationComponent.generated.h"

/**
 * The single persistence entry point on any actor. Adding this component opts the actor
 * into the persistence system. It registers with UPersistenceSubsystem, caches all
 * IPersistableComponent implementations at BeginPlay, handles dirty marking, and produces
 * FEntityPersistencePayload on demand.
 *
 * Server-side only. Must not replicate. One per actor.
 * The actor must implement ISourceIDInterface to provide a stable FGuid.
 */
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UPersistenceRegistrationComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    /**
     * Category tag for this actor. Routes payloads to the correct FOnPayloadReady delegate.
     * Must match a tag registered with UPersistenceSubsystem::RegisterPersistenceTag().
     * Set in Blueprint defaults or constructor.
     */
    UPROPERTY(EditDefaultsOnly, Category="Persistence",
              meta=(Categories="Persistence"))
    FGameplayTag PersistenceTag;

    /**
     * Incremented on every BuildPayload call.
     * Read by IPersistableComponent::NotifyDirty to stamp DirtyGeneration.
     * Used by ClearIfSaved to distinguish stale from fresh dirty state.
     */
    uint32 SaveGeneration = 0;

    /** Called by IPersistableComponent::NotifyDirty on any state change. */
    void MarkDirty();

    /**
     * Produces an FEntityPersistencePayload.
     * bFullSave=true  → serialize all components regardless of dirty state.
     * bFullSave=false → serialize only components with bDirty=true.
     * Must be called on the game thread only.
     */
    FEntityPersistencePayload BuildPayload(bool bFullSave = false);

    /** Returns the actor's stable GUID via ISourceIDInterface. */
    FGuid GetEntityGUID() const;

    /** Read-only access for UPersistenceSubsystem::OnRawPayloadReceived. */
    const TArray<TScriptInterface<IPersistableComponent>>& GetCachedPersistables() const
    {
        return CachedPersistables;
    }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UPROPERTY()
    TArray<TScriptInterface<IPersistableComponent>> CachedPersistables;

    bool bDirty = false;
};
