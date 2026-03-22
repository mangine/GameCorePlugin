// PersistenceRegistrationComponent.cpp
#include "PersistenceRegistrationComponent.h"
#include "Persistence/PersistenceSubsystem.h"
#include "Core/SourceID/SourceIDInterface.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogPersistence, Log, All);

// ---------------------------------------------------------------------------
// BeginPlay — Registration & Cache
// ---------------------------------------------------------------------------

void UPersistenceRegistrationComponent::BeginPlay()
{
    Super::BeginPlay();

#if WITH_EDITOR
    // Skip registration during PIE teardown
    if (GetWorld() && GetWorld()->WorldType == EWorldType::PIE
        && GetWorld()->bIsTearingDown)
        return;
#endif

    if (!PersistenceTag.IsValid())
    {
        UE_LOG(LogPersistence, Warning,
            TEXT("[%s] UPersistenceRegistrationComponent has no PersistenceTag. Actor will not persist."),
            *GetOwner()->GetName());
        return;
    }

    // Cache all persistable components once — no repeated queries
    for (UActorComponent* Comp : GetOwner()->GetComponents())
    {
        if (Comp->Implements<UPersistableComponent>())
            CachedPersistables.Add(Comp);
    }

#if !UE_BUILD_SHIPPING
    // Dev: validate unique persistence keys
    TSet<FName> Keys;
    for (auto& Ref : CachedPersistables)
    {
        FName Key = Ref.GetInterface()->GetPersistenceKey();
        bool bAlreadyInSet = false;
        Keys.Add(Key, &bAlreadyInSet);
        if (bAlreadyInSet)
            UE_LOG(LogPersistence, Error,
                TEXT("[%s] Duplicate PersistenceKey '%s' — only one blob will survive per save."),
                *GetOwner()->GetName(), *Key.ToString());
    }
#endif

    if (auto* Subsystem = GetGameInstance()->GetSubsystem<UPersistenceSubsystem>())
        Subsystem->RegisterEntity(this);
}

// ---------------------------------------------------------------------------
// EndPlay — Unregister & Immediate Save
// ---------------------------------------------------------------------------

void UPersistenceRegistrationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
#if WITH_EDITOR
    // Suppress spurious saves during PIE teardown
    if (Reason == EEndPlayReason::EndPlayInEditor)
    {
        Super::EndPlay(Reason);
        return;
    }
#endif

    if (auto* Subsystem = GetGameInstance()->GetSubsystem<UPersistenceSubsystem>())
    {
        if (bDirty)
        {
            // Serialize while UObjects are still valid.
            // RequestFullSave dispatches immediately through the normal delegate path.
            Subsystem->RequestFullSave(GetOwner(), ESerializationReason::Logout);
        }

        Subsystem->UnregisterEntity(GetEntityGUID());
    }

    Super::EndPlay(Reason);
}

// ---------------------------------------------------------------------------
// MarkDirty
// ---------------------------------------------------------------------------

void UPersistenceRegistrationComponent::MarkDirty()
{
    if (bDirty) return; // Guard: already queued, no-op

    bDirty = true;

    if (auto* Subsystem = GetGameInstance()->GetSubsystem<UPersistenceSubsystem>())
        Subsystem->EnqueueDirty(GetEntityGUID());
}

// ---------------------------------------------------------------------------
// BuildPayload
// ---------------------------------------------------------------------------

FEntityPersistencePayload UPersistenceRegistrationComponent::BuildPayload(bool bFullSave)
{
    FEntityPersistencePayload Payload;
    Payload.EntityId         = GetEntityGUID();
    Payload.ServerInstanceId = GetGameInstance()
                                ->GetSubsystem<UPersistenceSubsystem>()
                                ->ServerInstanceId;
    Payload.PersistenceTag   = PersistenceTag;
    Payload.Timestamp        = FDateTime::UtcNow().ToUnixTimestamp();

    ++SaveGeneration;

    for (auto& PersistableRef : CachedPersistables)
    {
        IPersistableComponent* Persistable = PersistableRef.GetInterface();
        if (!Persistable) continue;
        if (!bFullSave && !Persistable->IsDirty()) continue;

        FComponentPersistenceBlob Blob;
        Blob.Key     = Persistable->GetPersistenceKey();
        Blob.Version = Persistable->GetSchemaVersion();

        FMemoryWriter Writer(Blob.Data);
        Persistable->Serialize_Save(Writer);

        Payload.Components.Add(MoveTemp(Blob));
        Persistable->ClearIfSaved(SaveGeneration);
    }

    bDirty = false;
    return Payload;
}

// ---------------------------------------------------------------------------
// GetEntityGUID
// ---------------------------------------------------------------------------

FGuid UPersistenceRegistrationComponent::GetEntityGUID() const
{
    if (GetOwner()->Implements<USourceIDInterface>())
        return ISourceIDInterface::Execute_GetEntityGUID(GetOwner());

#if !UE_BUILD_SHIPPING
    UE_LOG(LogPersistence, Warning,
        TEXT("[%s] does not implement ISourceIDInterface. Cannot persist."),
        *GetOwner()->GetName());
#endif
    return FGuid();
}
