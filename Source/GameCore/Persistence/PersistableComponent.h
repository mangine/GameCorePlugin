// PersistableComponent.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PersistableComponent.generated.h"

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UPersistableComponent : public UInterface { GENERATED_BODY() };

/**
 * Interface any actor component implements to participate in the persistence system.
 * Handles serialization, deserialization, schema versioning, and dirty notification.
 * All orchestration is owned by UPersistenceSubsystem and UPersistenceRegistrationComponent.
 *
 * IMPORTANT: Dirty-tracking fields (bDirty, DirtyGeneration, CachedRegComp) and the
 * NotifyDirty helper MUST be declared on the implementing component class — NOT inherited
 * from this interface. UInterface cannot own UObject-managed instance data.
 */
class GAMECORE_API IPersistableComponent
{
    GENERATED_BODY()

public:
    /**
     * Unique key identifying this component's blob in the payload.
     * Must be stable across versions — never rename after shipping.
     */
    virtual FName GetPersistenceKey() const = 0;

    /** Current schema version. Increment when the serialized layout changes. */
    virtual uint32 GetSchemaVersion() const = 0;

    /**
     * Write current state into Ar.
     * Must be strictly read-only — no state mutation during serialization.
     */
    virtual void Serialize_Save(FArchive& Ar) = 0;

    /**
     * Read state from Ar.
     * SavedVersion is the version stored in the blob being loaded.
     * UPersistenceSubsystem calls Migrate() before this if versions mismatch.
     */
    virtual void Serialize_Load(FArchive& Ar, uint32 SavedVersion) = 0;

    /**
     * Called automatically before Serialize_Load when SavedVersion != GetSchemaVersion().
     * Ar is positioned at the start of the old blob.
     * Default no-op is safe for purely additive schema changes.
     */
    virtual void Migrate(FArchive& Ar, uint32 FromVersion, uint32 ToVersion) {}

    /**
     * Clear dirty state if no newer dirty has occurred since this flush.
     * Called by UPersistenceRegistrationComponent::BuildPayload after serializing this component.
     * Implementing class must provide bDirty and DirtyGeneration as instance members.
     */
    virtual void ClearIfSaved(uint32 FlushedGeneration) = 0;

    /**
     * Returns whether this component has unsaved state.
     * Required by UPersistenceRegistrationComponent::BuildPayload to check dirty state
     * without casting to the concrete type (UInterface cannot hold instance data).
     * Implementing class: return bDirty.
     */
    virtual bool IsDirty() const = 0;
};
