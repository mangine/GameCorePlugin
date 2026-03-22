// PersistenceSubsystem.cpp
#include "PersistenceSubsystem.h"
#include "Persistence/PersistenceRegistrationComponent.h"
#include "Persistence/PersistableComponent.h"
#include "Core/Backend/GameCoreBackend.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogPersistence, Log, All);

// ---------------------------------------------------------------------------
// ShouldCreateSubsystem — Server Only
// ---------------------------------------------------------------------------

bool UPersistenceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    UGameInstance* GI = Cast<UGameInstance>(Outer);
    if (!GI) return false;
    UWorld* World = GI->GetWorld();
    if (!World) return false;
    ENetMode NetMode = World->GetNetMode();
    return NetMode == NM_DedicatedServer || NetMode == NM_ListenServer;
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------

void UPersistenceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // ServerInstanceId must be configured before this point.
    // Use UE_LOG directly here — backend subsystem may not be live yet.
    if (!ServerInstanceId.IsValid())
        UE_LOG(LogPersistence, Error,
            TEXT("[Persistence] ServerInstanceId not configured. "
                 "Set via DefaultGame.ini. "
                 "Payloads will be stamped with invalid GUID."));

    FTimerManager& TM = GetWorld()->GetTimerManager();

    TM.SetTimer(SaveTimer, this,
        &UPersistenceSubsystem::FlushSaveCycle,
        SaveInterval, true);

    TM.SetTimer(LoadTimeoutTimer, this,
        &UPersistenceSubsystem::TickLoadTimeouts,
        5.f, true);
}

// ---------------------------------------------------------------------------
// Deinitialize
// ---------------------------------------------------------------------------

void UPersistenceSubsystem::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        FTimerManager& TM = World->GetTimerManager();
        TM.ClearTimer(SaveTimer);
        TM.ClearTimer(FullCycleTickTimer);
        TM.ClearTimer(LoadTimeoutTimer);
    }

    Super::Deinitialize();
}

// ---------------------------------------------------------------------------
// Tag Registration & Dispatch
// ---------------------------------------------------------------------------

void UPersistenceSubsystem::RegisterPersistenceTag(FGameplayTag Tag)
{
    if (!TagDelegates.Contains(Tag))
        TagDelegates.Add(Tag, FOnPayloadReady());
}

UPersistenceSubsystem::FOnPayloadReady* UPersistenceSubsystem::GetSaveDelegate(FGameplayTag Tag)
{
    return TagDelegates.Find(Tag);
}

void UPersistenceSubsystem::DispatchPayload(const FEntityPersistencePayload& Payload)
{
    FOnPayloadReady* Delegate = TagDelegates.Find(Payload.PersistenceTag);
    if (!Delegate)
    {
        FGameCoreBackend::GetLogging(FGameplayTag()).LogError(
            FString::Printf(TEXT("[Persistence] No delegate for tag [%s]. Payload for entity [%s] dropped."),
                *Payload.PersistenceTag.ToString(),
                *Payload.EntityId.ToString()));
        return;
    }
    Delegate->Broadcast(Payload);
}

// ---------------------------------------------------------------------------
// Entity Registry
// ---------------------------------------------------------------------------

void UPersistenceSubsystem::RegisterEntity(UPersistenceRegistrationComponent* RegComp)
{
    if (!RegComp) return;
    FGuid ID = RegComp->GetEntityGUID();
    if (!ID.IsValid())
    {
        FGameCoreBackend::GetLogging(FGameplayTag()).LogWarning(
            TEXT("[Persistence] RegisterEntity called with invalid GUID. Actor will not persist."));
        return;
    }
    RegisteredEntities.Add(ID, RegComp);
}

void UPersistenceSubsystem::UnregisterEntity(FGuid EntityId)
{
    RegisteredEntities.Remove(EntityId);
    DirtySet.Remove(EntityId);
}

void UPersistenceSubsystem::EnqueueDirty(FGuid EntityId)
{
    DirtySet.Add(EntityId);
}

// ---------------------------------------------------------------------------
// FlushSaveCycle — Cycle Decision
// ---------------------------------------------------------------------------

void UPersistenceSubsystem::FlushSaveCycle()
{
    ++SaveCounter;
    const bool bFullSave = (SaveCounter % (PartialSavesBetweenFullSave + 1) == 0);

    if (bFullSave)
    {
        if (bFullCycleInProgress)
        {
            // Previous full cycle still running — skip this trigger.
            // Avoids double snapshot if SaveInterval is shorter than full cycle completion time.
            return;
        }

        RegisteredEntities.GetKeys(FullCycleEntitySnapshot);
        FullCycleCursorIndex = 0;
        bFullCycleInProgress = true;

        GetWorld()->GetTimerManager().SetTimer(
            FullCycleTickTimer, this,
            &UPersistenceSubsystem::TickFullCycle,
            0.0f, true);
    }
    else
    {
        FlushPartialCycle();
    }
}

// ---------------------------------------------------------------------------
// TickFullCycle — Spread Full Save Across Ticks
// ---------------------------------------------------------------------------

void UPersistenceSubsystem::TickFullCycle()
{
    int32 Processed = 0;

    while (FullCycleCursorIndex < FullCycleEntitySnapshot.Num()
           && Processed < ActorsPerFlushTick)
    {
        const FGuid& ID = FullCycleEntitySnapshot[FullCycleCursorIndex++];
        auto* RegCompPtr = RegisteredEntities.Find(ID);
        if (!RegCompPtr || !RegCompPtr->IsValid()) { continue; }

        FEntityPersistencePayload Payload = RegCompPtr->Get()->BuildPayload(true);
        Payload.PayloadType       = EPayloadType::Full;
        Payload.SaveReason        = ESerializationReason::Periodic;
        Payload.bCritical         = false;
        Payload.bFlushImmediately = false;
        DirtySet.Remove(ID);
        ++Processed;

        DispatchPayload(Payload);
    }

    if (FullCycleCursorIndex >= FullCycleEntitySnapshot.Num())
    {
        bFullCycleInProgress = false;
        FullCycleEntitySnapshot.Reset();
        GetWorld()->GetTimerManager().ClearTimer(FullCycleTickTimer);
    }
}

// ---------------------------------------------------------------------------
// FlushPartialCycle — Dirty Actors Within Budget
// ---------------------------------------------------------------------------

void UPersistenceSubsystem::FlushPartialCycle()
{
    TArray<FGuid> ToRemove;
    int32 Processed = 0;

    for (const FGuid& ID : DirtySet)
    {
        if (Processed >= ActorsPerFlushTick) break;

        auto* RegCompPtr = RegisteredEntities.Find(ID);
        if (!RegCompPtr || !RegCompPtr->IsValid())
        {
            ToRemove.Add(ID);
            continue;
        }

        FEntityPersistencePayload Payload = RegCompPtr->Get()->BuildPayload(false);
        Payload.PayloadType       = EPayloadType::Partial;
        Payload.SaveReason        = ESerializationReason::Periodic;
        Payload.bCritical         = false;
        Payload.bFlushImmediately = false;
        ToRemove.Add(ID);
        ++Processed;

        DispatchPayload(Payload);
    }

    for (const FGuid& ID : ToRemove)
        DirtySet.Remove(ID);
}

// ---------------------------------------------------------------------------
// RequestFullSave — Single Actor Event
// ---------------------------------------------------------------------------

bool UPersistenceSubsystem::IsCriticalReason(ESerializationReason Reason)
{
    return Reason == ESerializationReason::Logout
        || Reason == ESerializationReason::ZoneTransfer
        || Reason == ESerializationReason::ServerShutdown;
}

bool UPersistenceSubsystem::IsImmediateReason(ESerializationReason Reason)
{
    return Reason == ESerializationReason::Logout
        || Reason == ESerializationReason::ServerShutdown;
}

void UPersistenceSubsystem::RequestFullSave(AActor* Entity, ESerializationReason Reason)
{
    if (!Entity) return;

    auto* RegComp = Entity->FindComponentByClass<UPersistenceRegistrationComponent>();
    if (!RegComp) return;

    FEntityPersistencePayload Payload = RegComp->BuildPayload(true);
    Payload.PayloadType       = EPayloadType::Full;
    Payload.SaveReason        = Reason;
    Payload.bCritical         = IsCriticalReason(Reason);
    Payload.bFlushImmediately = IsImmediateReason(Reason);

    DirtySet.Remove(RegComp->GetEntityGUID());
    DispatchPayload(Payload);
}

// ---------------------------------------------------------------------------
// RequestShutdownSave — All Actors, Synchronous
// ---------------------------------------------------------------------------

void UPersistenceSubsystem::RequestShutdownSave()
{
    FTimerManager& TM = GetWorld()->GetTimerManager();
    TM.ClearTimer(SaveTimer);
    TM.ClearTimer(FullCycleTickTimer);

    bFullCycleInProgress = false;
    FullCycleEntitySnapshot.Reset();

    for (auto& [ID, RegCompPtr] : RegisteredEntities)
    {
        if (!RegCompPtr.IsValid()) continue;

        FEntityPersistencePayload Payload = RegCompPtr->BuildPayload(true);
        Payload.PayloadType       = EPayloadType::Full;
        Payload.SaveReason        = ESerializationReason::ServerShutdown;
        Payload.bCritical         = true;
        Payload.bFlushImmediately = true;

        DispatchPayload(Payload);
    }

    DirtySet.Empty();
}

// ---------------------------------------------------------------------------
// Load Path
// ---------------------------------------------------------------------------

void UPersistenceSubsystem::RequestLoad(FGuid EntityId, FGameplayTag Tag,
    TFunction<void(bool)> OnComplete)
{
    LoadCallbacks.Add(EntityId,
        { MoveTemp(OnComplete), (float)FPlatformTime::Seconds() });
    OnLoadRequested.Broadcast(EntityId, Tag);
}

void UPersistenceSubsystem::OnRawPayloadReceived(AActor* Actor,
    const FEntityPersistencePayload& Payload)
{
    auto* RegComp = Actor->FindComponentByClass<UPersistenceRegistrationComponent>();
    if (!RegComp) return;

    for (const FComponentPersistenceBlob& Blob : Payload.Components)
    {
        IPersistableComponent* Target = nullptr;
        for (auto& Ref : RegComp->GetCachedPersistables())
        {
            if (Ref.GetInterface()->GetPersistenceKey() == Blob.Key)
            {
                Target = Ref.GetInterface();
                break;
            }
        }
        if (!Target) continue;

        FMemoryReader Reader(Blob.Data);
        const uint32 SavedVersion   = Blob.Version;
        const uint32 CurrentVersion = Target->GetSchemaVersion();

        if (SavedVersion != CurrentVersion)
            Target->Migrate(Reader, SavedVersion, CurrentVersion);

        Target->Serialize_Load(Reader, SavedVersion);
    }

    if (auto* Request = LoadCallbacks.Find(Payload.EntityId))
    {
        Request->Callback(true);
        LoadCallbacks.Remove(Payload.EntityId);
    }
}

void UPersistenceSubsystem::OnLoadFailed(FGuid EntityId)
{
    if (auto* Request = LoadCallbacks.Find(EntityId))
    {
        FGameCoreBackend::GetLogging(FGameplayTag()).LogError(
            FString::Printf(TEXT("[Persistence] Load failed for entity [%s]."),
                *EntityId.ToString()));
        Request->Callback(false);
        LoadCallbacks.Remove(EntityId);
    }
}

// ---------------------------------------------------------------------------
// Load Timeout Sweep
// ---------------------------------------------------------------------------

void UPersistenceSubsystem::TickLoadTimeouts()
{
    const float Now = (float)FPlatformTime::Seconds();
    TArray<FGuid> Expired;

    for (auto& [ID, Request] : LoadCallbacks)
    {
        if (Now - Request.Timestamp >= LoadTimeoutSeconds)
            Expired.Add(ID);
    }

    for (const FGuid& ID : Expired)
    {
        FGameCoreBackend::GetLogging(FGameplayTag()).LogError(
            FString::Printf(TEXT("[Persistence] Load timed out for entity [%s]. Firing OnComplete(false)."),
                *ID.ToString()));
        LoadCallbacks[ID].Callback(false);
        LoadCallbacks.Remove(ID);
    }
}
