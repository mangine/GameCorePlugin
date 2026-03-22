#include "Quest/Components/QuestComponent.h"
#include "Quest/Data/QuestDefinition.h"
#include "Quest/Data/QuestStageDefinition.h"
#include "Quest/Subsystems/QuestRegistrySubsystem.h"
#include "Quest/StateMachine/QuestTransitionContext.h"
#include "Quest/Events/QuestEventPayloads.h"
#include "EventBus/GameCoreEventBus.h"
#include "EventBus/GameCoreEventWatcher.h"
#include "Requirements/RequirementContext.h"
#include "Net/UnrealNetwork.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerState.h"

DEFINE_LOG_CATEGORY_STATIC(LogQuest, Log, All);

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void UQuestComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!QuestConfig.IsNull())
        LoadedConfig = QuestConfig.LoadSynchronous();

    if (GetOwnerRole() == ROLE_Authority)
    {
        ValidateActiveQuestsOnLogin();
    }
    else if (GetOwnerRole() == ROLE_AutonomousProxy)
    {
        RegisterClientValidatedCompletionWatchers();
    }
}

void UQuestComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME_CONDITION(UQuestComponent, ActiveQuests,       COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UQuestComponent, CompletedQuestTags, COND_OwnerOnly);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

UQuestRegistrySubsystem* UQuestComponent::GetRegistry() const
{
    if (UGameInstance* GI = GetGameInstance())
        return GI->GetSubsystem<UQuestRegistrySubsystem>();
    return nullptr;
}

FQuestRuntime* UQuestComponent::FindActiveQuest(const FGameplayTag& QuestId)
{
    return ActiveQuests.Items.FindByPredicate(
        [&](const FQuestRuntime& R){ return R.QuestId == QuestId; });
}

const FQuestRuntime* UQuestComponent::FindActiveQuest(const FGameplayTag& QuestId) const
{
    return ActiveQuests.Items.FindByPredicate(
        [&](const FQuestRuntime& R){ return R.QuestId == QuestId; });
}

int32 UQuestComponent::GetMaxActiveQuests() const
{
    return LoadedConfig ? LoadedConfig->MaxActiveQuests : 20;
}

FRequirementContext UQuestComponent::BuildRequirementContext() const
{
    FQuestEvaluationContext CtxData;
    CtxData.PlayerState = GetOwner<APlayerState>();
    CtxData.World       = GetWorld();
    return FRequirementContext::Make(CtxData);
}

FGameplayTag UQuestComponent::ResolveNextStage(
    const FQuestRuntime& Runtime,
    const UQuestDefinition* Def) const
{
    if (!Def || !Def->StageGraph) return FGameplayTag();

    UQuestTransitionContext* CtxWrapper =
        NewObject<UQuestTransitionContext>(GetTransientPackage());
    CtxWrapper->Context = BuildRequirementContext();

    return Def->StageGraph->FindFirstPassingTransition(
        Runtime.CurrentStageTag, CtxWrapper);
}

bool UQuestComponent::ShouldWatchUnlock(
    const FGameplayTag& QuestId, const UQuestDefinition* Def) const
{
    if (!Def || !Def->bEnabled) return false;
    if (FindActiveQuest(QuestId) != nullptr) return false;

    switch (Def->Lifecycle)
    {
    case EQuestLifecycle::SingleAttempt:
    case EQuestLifecycle::RetryUntilComplete:
        if (CompletedQuestTags.HasTag(Def->QuestCompletedTag))
            return false;
        break;
    case EQuestLifecycle::RetryAndAssist:
    case EQuestLifecycle::Evergreen:
        break;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Replication callbacks
// ---------------------------------------------------------------------------

void UQuestComponent::OnRep_CompletedQuestTags()
{
    // UI listens via OnQuestEvent delegate — no direct action needed here.
}

// ---------------------------------------------------------------------------
// Login Validation
// ---------------------------------------------------------------------------

void UQuestComponent::ValidateActiveQuestsOnLogin()
{
    TArray<FGameplayTag> QuestIds;
    for (const FQuestRuntime& R : ActiveQuests.Items)
        QuestIds.Add(R.QuestId);

    if (QuestIds.IsEmpty())
    {
        RegisterUnlockWatchers();
        return;
    }

    TSharedRef<int32> Pending = MakeShared<int32>(QuestIds.Num());
    TWeakObjectPtr<UQuestComponent> WeakThis = this;

    UQuestRegistrySubsystem* Registry = GetRegistry();
    if (!Registry)
    {
        RegisterUnlockWatchers();
        return;
    }

    for (const FGameplayTag& QuestId : QuestIds)
    {
        Registry->GetOrLoadDefinitionAsync(QuestId,
            [WeakThis, QuestId, Pending](const UQuestDefinition* Def)
            {
                UQuestComponent* QC = WeakThis.Get();
                if (!QC) return;

                if (!Def || !Def->bEnabled)
                {
                    QC->ActiveQuests.Items.RemoveAll(
                        [&](const FQuestRuntime& R){ return R.QuestId == QuestId; });
                    UE_LOG(LogQuest, Warning,
                        TEXT("Removed disabled quest '%s' on login."),
                        *QuestId.ToString());
                }

                if (--(*Pending) == 0)
                {
                    if (!QC->ActiveQuests.Items.IsEmpty())
                        QC->ActiveQuests.MarkArrayDirty();
                    QC->RegisterUnlockWatchers();
                }
            });
    }
}

// ---------------------------------------------------------------------------
// Unlock Watchers
// ---------------------------------------------------------------------------

void UQuestComponent::RegisterUnlockWatchers()
{
    UQuestRegistrySubsystem* Registry = GetRegistry();
    if (!Registry) return;

    Registry->IterateAllDefinitions(
        [this](const FGameplayTag& QuestId, const UQuestDefinition* Def)
        {
            if (!ShouldWatchUnlock(QuestId, Def)) return;
            if (!Def->UnlockRequirements) return;
            RegisterUnlockWatcherForQuest(QuestId, Def);
        });
}

void UQuestComponent::RegisterUnlockWatcherForQuest(
    const FGameplayTag& QuestId, const UQuestDefinition* Def)
{
    TWeakObjectPtr<UQuestComponent> WeakThis = this;

    FEventWatchHandle Handle = Def->UnlockRequirements->RegisterWatch(
        this,
        [WeakThis, QuestId](bool bPassed)
        {
            if (UQuestComponent* QC = WeakThis.Get())
            {
                QC->ClientRPC_NotifyQuestEvent(QuestId,
                    bPassed
                        ? EQuestEventType::BecameAvailable
                        : EQuestEventType::BecameUnavailable);
            }
        });

    UnlockWatcherHandles.Add(QuestId, Handle);

    // Immediate baseline check.
    FRequirementContext Ctx = BuildRequirementContext();
    FRequirementResult Result = Def->UnlockRequirements->Evaluate(Ctx);
    ClientRPC_NotifyQuestEvent(QuestId,
        Result.bPassed
            ? EQuestEventType::BecameAvailable
            : EQuestEventType::BecameUnavailable);
}

void UQuestComponent::RegisterClientValidatedCompletionWatchers()
{
    for (const FQuestRuntime& Runtime : ActiveQuests.Items)
        RegisterClientValidatedCompletionWatcher(Runtime);
}

void UQuestComponent::RegisterClientValidatedCompletionWatcher(
    const FQuestRuntime& Runtime)
{
    UQuestRegistrySubsystem* Registry = GetRegistry();
    if (!Registry) return;

    Registry->GetOrLoadDefinitionAsync(Runtime.QuestId,
        [WeakThis = TWeakObjectPtr<UQuestComponent>(this),
         QuestId = Runtime.QuestId](const UQuestDefinition* Def)
        {
            UQuestComponent* QC = WeakThis.Get();
            if (!QC || !Def) return;
            if (Def->CheckAuthority != EQuestCheckAuthority::ClientValidated) return;

            // Register watcher for current stage completion requirements.
            const FQuestRuntime* R = QC->FindActiveQuest(QuestId);
            if (!R) return;

            const UQuestStageDefinition* Stage = Def->FindStage(R->CurrentStageTag);
            if (!Stage || !Stage->CompletionRequirements) return;

            Stage->CompletionRequirements->RegisterWatch(QC,
                [WeakThis, QuestId, StageTag = R->CurrentStageTag](bool bPassed)
                {
                    UQuestComponent* Comp = WeakThis.Get();
                    if (!Comp || !bPassed) return;
                    Comp->ServerRPC_RequestValidation(QuestId, StageTag);
                });
        });
}

// ---------------------------------------------------------------------------
// Accept Flow
// ---------------------------------------------------------------------------

void UQuestComponent::ServerRPC_AcceptQuest_Implementation(FGameplayTag QuestId)
{
    if (ActiveQuests.Items.Num() >= GetMaxActiveQuests())
    {
        ClientRPC_NotifyValidationRejected(QuestId, FGameplayTag(), EQuestRejectionReason::AtCapacity);
        return;
    }
    if (FindActiveQuest(QuestId))
    {
        ClientRPC_NotifyValidationRejected(QuestId, FGameplayTag(), EQuestRejectionReason::AlreadyActive);
        return;
    }

    UQuestRegistrySubsystem* Registry = GetRegistry();
    if (!Registry) return;

    Registry->GetOrLoadDefinitionAsync(QuestId,
        [WeakThis = TWeakObjectPtr<UQuestComponent>(this), QuestId](const UQuestDefinition* Def)
        {
            UQuestComponent* QC = WeakThis.Get();
            if (!QC) return;

            if (!Def || !Def->bEnabled)
            {
                QC->ClientRPC_NotifyValidationRejected(
                    QuestId, FGameplayTag(), EQuestRejectionReason::QuestDisabled);
                return;
            }

            if (Def->Lifecycle == EQuestLifecycle::SingleAttempt &&
                QC->CompletedQuestTags.HasTag(Def->QuestCompletedTag))
            {
                QC->ClientRPC_NotifyValidationRejected(
                    QuestId, FGameplayTag(), EQuestRejectionReason::PermanentlyClosed);
                return;
            }

            FRequirementContext Ctx = QC->BuildRequirementContext();
            if (Def->UnlockRequirements)
            {
                FRequirementResult Result = Def->UnlockRequirements->Evaluate(Ctx);
                if (!Result.bPassed)
                {
                    QC->ClientRPC_NotifyValidationRejected(
                        QuestId, FGameplayTag(), EQuestRejectionReason::ConditionFailed);
                    return;
                }
            }

            if (!Def->StageGraph) return;
            const FGameplayTag EntryState = Def->StageGraph->EntryStateTag;
            if (!EntryState.IsValid()) return;

            FQuestRuntime Runtime;
            Runtime.QuestId         = QuestId;
            Runtime.CurrentStageTag = EntryState;

            const UQuestStageDefinition* EntryStage = Def->FindStage(EntryState);
            QC->Internal_InitTrackers(Runtime, EntryStage);

            QC->ActiveQuests.Items.Add(Runtime);
            QC->ActiveQuests.MarkArrayDirty();

            QC->GetRegistry()->AddReference(QuestId);
            NotifyDirty(QC);

            // Unregister unlock watcher.
            if (FEventWatchHandle* H = QC->UnlockWatcherHandles.Find(QuestId))
            {
                if (UGameCoreEventWatcher* Watcher = UGameCoreEventWatcher::Get(QC))
                    Watcher->Unregister(*H);
                QC->UnlockWatcherHandles.Remove(QuestId);
            }

            // Broadcast events.
            FQuestStartedPayload StartPayload;
            StartPayload.QuestId     = QuestId;
            StartPayload.PlayerState = QC->GetOwner<APlayerState>();
            if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(QC))
                Bus->Broadcast(TAG_GameCoreEvent_Quest_Started, StartPayload);

            FQuestStageChangedPayload StagePayload;
            StagePayload.QuestId     = QuestId;
            StagePayload.StageTag    = EntryState;
            StagePayload.PlayerState = QC->GetOwner<APlayerState>();
            if (EntryStage)
                StagePayload.ObjectiveText = EntryStage->StageObjectiveText;
            if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(QC))
                Bus->Broadcast(TAG_GameCoreEvent_Quest_StageStarted, StagePayload);

            QC->ClientRPC_NotifyQuestEvent(QuestId, EQuestEventType::Started);
        });
}

// ---------------------------------------------------------------------------
// Abandon
// ---------------------------------------------------------------------------

void UQuestComponent::ServerRPC_AbandonQuest_Implementation(FGameplayTag QuestId)
{
    FQuestRuntime* Runtime = FindActiveQuest(QuestId);
    if (!Runtime) return;

    UQuestRegistrySubsystem* Registry = GetRegistry();
    if (Registry) Registry->ReleaseReference(QuestId);

    ActiveQuests.Items.RemoveAll(
        [&](const FQuestRuntime& R){ return R.QuestId == QuestId; });
    ActiveQuests.MarkArrayDirty();
    NotifyDirty(this);

    FQuestAbandonedPayload Payload;
    Payload.QuestId     = QuestId;
    Payload.PlayerState = GetOwner<APlayerState>();
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
        Bus->Broadcast(TAG_GameCoreEvent_Quest_Abandoned, Payload);

    ClientRPC_NotifyQuestEvent(QuestId, EQuestEventType::Abandoned);
}

// ---------------------------------------------------------------------------
// Validation RPC
// ---------------------------------------------------------------------------

void UQuestComponent::ServerRPC_RequestValidation_Implementation(
    FGameplayTag QuestId, FGameplayTag StageTag)
{
    FQuestRuntime* Runtime = FindActiveQuest(QuestId);
    if (!Runtime || Runtime->CurrentStageTag != StageTag)
    {
        ClientRPC_NotifyValidationRejected(
            QuestId, StageTag, EQuestRejectionReason::StageMismatch);
        return;
    }
    EvaluateCompletionRequirementsNow(QuestId);
}

// ---------------------------------------------------------------------------
// Tracker Increment
// ---------------------------------------------------------------------------

void UQuestComponent::Server_IncrementTracker(
    const FGameplayTag& QuestId,
    const FGameplayTag& TrackerKey,
    int32 Delta)
{
    FQuestRuntime* Runtime = FindActiveQuest(QuestId);
    if (!Runtime) return;

    FQuestTrackerEntry* Tracker = Runtime->FindTracker(TrackerKey);
    if (!Tracker) return;

    const int32 OldValue = Tracker->CurrentValue;
    Tracker->CurrentValue = FMath::Min(
        Tracker->CurrentValue + Delta, Tracker->EffectiveTarget);

    if (Tracker->CurrentValue == OldValue) return; // clamped, no-op

    ActiveQuests.MarkItemDirty(*Runtime);
    NotifyDirty(this);

    FQuestTrackerUpdatedPayload Payload;
    Payload.QuestId         = QuestId;
    Payload.TrackerKey      = TrackerKey;
    Payload.OldValue        = OldValue;
    Payload.NewValue        = Tracker->CurrentValue;
    Payload.EffectiveTarget = Tracker->EffectiveTarget;
    Payload.PlayerState     = GetOwner<APlayerState>();
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
        Bus->Broadcast(TAG_GameCoreEvent_Quest_TrackerUpdated, Payload);

    OnTrackerUpdated.Broadcast(QuestId, TrackerKey, Tracker->CurrentValue);

    if (Tracker->CurrentValue >= Tracker->EffectiveTarget)
        EvaluateCompletionRequirementsNow(QuestId);
}

// ---------------------------------------------------------------------------
// Completion Evaluation
// ---------------------------------------------------------------------------

void UQuestComponent::EvaluateCompletionRequirementsNow(const FGameplayTag& QuestId)
{
    const FQuestRuntime* Runtime = FindActiveQuest(QuestId);
    if (!Runtime) return;

    UQuestRegistrySubsystem* Registry = GetRegistry();
    const UQuestDefinition* Def = Registry ? Registry->GetDefinition(QuestId) : nullptr;
    if (!Def) return;

    const UQuestStageDefinition* Stage = Def->FindStage(Runtime->CurrentStageTag);
    if (!Stage || !Stage->CompletionRequirements) return;

    FRequirementContext Ctx = BuildRequirementContext();
    FRequirementResult Result = Stage->CompletionRequirements->Evaluate(Ctx);
    if (!Result.bPassed) return;

    FQuestRuntime* MutableRuntime = FindActiveQuest(QuestId);
    if (!MutableRuntime) return;

    if (Stage->bIsCompletionState)
        Internal_CompleteQuest(*MutableRuntime, Def);
    else if (Stage->bIsFailureState)
        Internal_FailQuest(*MutableRuntime, Def);
    else
    {
        const FGameplayTag Next = ResolveNextStage(*MutableRuntime, Def);
        if (Next.IsValid())
            Internal_AdvanceStage(*MutableRuntime, Def, Next);
    }
}

// ---------------------------------------------------------------------------
// Complete / Fail / AdvanceStage / InitTrackers
// ---------------------------------------------------------------------------

void UQuestComponent::Internal_CompleteQuest(
    FQuestRuntime& Runtime, const UQuestDefinition* Def)
{
    Runtime.LastCompletedTimestamp = FDateTime::UtcNow().ToUnixTimestamp();

    UQuestRegistrySubsystem* Registry = GetRegistry();
    if (Registry) Registry->ReleaseReference(Runtime.QuestId);

    const bool bPermanent =
        Def->Lifecycle == EQuestLifecycle::SingleAttempt ||
        Def->Lifecycle == EQuestLifecycle::RetryUntilComplete;

    if (bPermanent)
    {
        CompletedQuestTags.AddTag(Def->QuestCompletedTag);
        if (FEventWatchHandle* H = UnlockWatcherHandles.Find(Def->QuestId))
        {
            if (UGameCoreEventWatcher* Watcher = UGameCoreEventWatcher::Get(this))
                Watcher->Unregister(*H);
            UnlockWatcherHandles.Remove(Def->QuestId);
        }
    }

    const FGameplayTag QuestId = Runtime.QuestId;
    ActiveQuests.Items.RemoveAll(
        [&](const FQuestRuntime& R){ return R.QuestId == QuestId; });
    ActiveQuests.MarkArrayDirty();

    if (!bPermanent && Registry)
    {
        if (const UQuestDefinition* Loaded = Registry->GetDefinition(QuestId))
            if (ShouldWatchUnlock(QuestId, Loaded))
                RegisterUnlockWatcherForQuest(QuestId, Loaded);
    }

    NotifyDirty(this);

    FQuestCompletedPayload Payload;
    Payload.QuestId     = QuestId;
    Payload.PlayerState = GetOwner<APlayerState>();
    Payload.RewardTable = bPermanent
        ? Def->FirstTimeRewardTable : Def->RepeatingRewardTable;
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
        Bus->Broadcast(TAG_GameCoreEvent_Quest_Completed, Payload);

    ClientRPC_NotifyQuestEvent(QuestId, EQuestEventType::Completed);
}

void UQuestComponent::Internal_FailQuest(
    FQuestRuntime& Runtime, const UQuestDefinition* Def)
{
    UQuestRegistrySubsystem* Registry = GetRegistry();
    if (Registry) Registry->ReleaseReference(Runtime.QuestId);

    const bool bPermanent =
        Def->Lifecycle == EQuestLifecycle::SingleAttempt;

    if (bPermanent)
    {
        CompletedQuestTags.AddTag(Def->QuestCompletedTag);
        if (FEventWatchHandle* H = UnlockWatcherHandles.Find(Def->QuestId))
        {
            if (UGameCoreEventWatcher* Watcher = UGameCoreEventWatcher::Get(this))
                Watcher->Unregister(*H);
            UnlockWatcherHandles.Remove(Def->QuestId);
        }
    }

    const FGameplayTag QuestId = Runtime.QuestId;
    ActiveQuests.Items.RemoveAll(
        [&](const FQuestRuntime& R){ return R.QuestId == QuestId; });
    ActiveQuests.MarkArrayDirty();

    if (!bPermanent && Registry)
    {
        if (const UQuestDefinition* Loaded = Registry->GetDefinition(QuestId))
            if (ShouldWatchUnlock(QuestId, Loaded))
                RegisterUnlockWatcherForQuest(QuestId, Loaded);
    }

    NotifyDirty(this);

    FQuestFailedPayload Payload;
    Payload.QuestId            = QuestId;
    Payload.PlayerState        = GetOwner<APlayerState>();
    Payload.bPermanentlyClosed = bPermanent;
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
        Bus->Broadcast(TAG_GameCoreEvent_Quest_Failed, Payload);

    ClientRPC_NotifyQuestEvent(QuestId, EQuestEventType::Failed);
}

void UQuestComponent::Internal_AdvanceStage(
    FQuestRuntime& Runtime,
    const UQuestDefinition* Def,
    const FGameplayTag& NewStageTag)
{
    Runtime.CurrentStageTag = NewStageTag;
    Runtime.Trackers.Empty();

    const UQuestStageDefinition* NewStage = Def->FindStage(NewStageTag);
    Internal_InitTrackers(Runtime, NewStage);

    ActiveQuests.MarkItemDirty(Runtime);
    NotifyDirty(this);

    FQuestStageChangedPayload Payload;
    Payload.QuestId       = Runtime.QuestId;
    Payload.StageTag      = NewStageTag;
    Payload.PlayerState   = GetOwner<APlayerState>();
    if (NewStage)
        Payload.ObjectiveText = NewStage->StageObjectiveText;
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
        Bus->Broadcast(TAG_GameCoreEvent_Quest_StageStarted, Payload);

    ClientRPC_NotifyQuestEvent(Runtime.QuestId, EQuestEventType::StageAdvanced);

    // Re-evaluate completion immediately (handles stages with no trackers).
    if (NewStage)
    {
        if (NewStage->bIsCompletionState)
            EvaluateCompletionRequirementsNow(Runtime.QuestId);
        else if (NewStage->bIsFailureState)
            EvaluateCompletionRequirementsNow(Runtime.QuestId);
    }
}

void UQuestComponent::Internal_InitTrackers(
    FQuestRuntime& Runtime,
    const UQuestStageDefinition* StageDef,
    int32 GroupSize)
{
    if (!StageDef) return;

    for (const FQuestProgressTrackerDef& TrackerDef : StageDef->Trackers)
    {
        if (TrackerDef.bReEvaluateOnly) continue;

        FQuestTrackerEntry Entry;
        Entry.TrackerKey      = TrackerDef.TrackerKey;
        Entry.CurrentValue    = 0;
        Entry.EffectiveTarget = TrackerDef.GetEffectiveTarget(GroupSize);
        Runtime.Trackers.Add(Entry);
    }
}

// ---------------------------------------------------------------------------
// Force Complete / Fail (admin/debug)
// ---------------------------------------------------------------------------

void UQuestComponent::Server_ForceCompleteQuest(const FGameplayTag& QuestId)
{
    FQuestRuntime* Runtime = FindActiveQuest(QuestId);
    if (!Runtime) return;

    UQuestRegistrySubsystem* Registry = GetRegistry();
    const UQuestDefinition* Def = Registry ? Registry->GetDefinition(QuestId) : nullptr;
    if (!Def) return;

    Internal_CompleteQuest(*Runtime, Def);
}

void UQuestComponent::Server_ForceFailQuest(const FGameplayTag& QuestId)
{
    FQuestRuntime* Runtime = FindActiveQuest(QuestId);
    if (!Runtime) return;

    UQuestRegistrySubsystem* Registry = GetRegistry();
    const UQuestDefinition* Def = Registry ? Registry->GetDefinition(QuestId) : nullptr;
    if (!Def) return;

    Internal_FailQuest(*Runtime, Def);
}

// ---------------------------------------------------------------------------
// Cadence Reset
// ---------------------------------------------------------------------------

void UQuestComponent::FlushCadenceResets(EQuestResetCadence Cadence)
{
    UQuestRegistrySubsystem* Registry = GetRegistry();
    if (!Registry) return;

    Registry->IterateAllDefinitions(
        [this, Cadence](const FGameplayTag& QuestId, const UQuestDefinition* Def)
        {
            if (!Def || Def->ResetCadence != Cadence) return;
            CompletedQuestTags.RemoveTag(Def->QuestCompletedTag);

            if (ShouldWatchUnlock(QuestId, Def) && Def->UnlockRequirements)
                RegisterUnlockWatcherForQuest(QuestId, Def);
        });
}

// ---------------------------------------------------------------------------
// Client RPCs
// ---------------------------------------------------------------------------

void UQuestComponent::ClientRPC_NotifyQuestEvent_Implementation(
    FGameplayTag QuestId, EQuestEventType EventType)
{
    OnQuestEvent.Broadcast(QuestId, EventType);
}

void UQuestComponent::ClientRPC_NotifyValidationRejected_Implementation(
    FGameplayTag QuestId, FGameplayTag StageTag, EQuestRejectionReason Reason)
{
    // Broadcast for UI to display rejection reason.
    OnQuestEvent.Broadcast(QuestId, EQuestEventType::BecameUnavailable);
}

// ---------------------------------------------------------------------------
// FFastArraySerializer callbacks
// ---------------------------------------------------------------------------

void FQuestRuntime::PostReplicatedAdd(const FQuestRuntimeArray& Array)
{
    // Owner component registers ClientValidated watchers for newly added quests.
    // Array.Owner is not accessible from here; UQuestComponent handles this via
    // OnRep path. Watcher registration is done in RegisterClientValidatedCompletionWatcher.
}

void FQuestRuntime::PostReplicatedChange(const FQuestRuntimeArray& Array)
{
}

void FQuestRuntime::PreReplicatedRemove(const FQuestRuntimeArray& Array)
{
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void UQuestComponent::Serialize_Save(FArchive& Ar)
{
    TArray<FName> TagNames;
    for (const FGameplayTag& T : CompletedQuestTags)
        TagNames.Add(T.GetTagName());
    Ar << TagNames;

    int32 Count = ActiveQuests.Items.Num();
    Ar << Count;
    for (const FQuestRuntime& Q : ActiveQuests.Items)
    {
        FName QuestIdName  = Q.QuestId.GetTagName();
        FName StageTagName = Q.CurrentStageTag.GetTagName();
        Ar << QuestIdName << StageTagName;
        Ar << const_cast<int64&>(Q.LastCompletedTimestamp);

        int32 TrackerCount = Q.Trackers.Num();
        Ar << TrackerCount;
        for (const FQuestTrackerEntry& T : Q.Trackers)
        {
            FName TrackerKeyName = T.TrackerKey.GetTagName();
            Ar << TrackerKeyName
               << const_cast<int32&>(T.CurrentValue)
               << const_cast<int32&>(T.EffectiveTarget);
        }
    }
}

void UQuestComponent::Serialize_Load(FArchive& Ar, uint32 SavedVersion)
{
    TArray<FName> TagNames;
    Ar << TagNames;
    CompletedQuestTags.Reset();
    for (const FName& Name : TagNames)
    {
        FGameplayTag Tag = FGameplayTag::RequestGameplayTag(Name, false);
        if (Tag.IsValid())
            CompletedQuestTags.AddTag(Tag);
        else
            UE_LOG(LogQuest, Warning,
                TEXT("Serialize_Load: Unknown CompletedQuestTag '%s' — skipped."),
                *Name.ToString());
    }

    int32 Count = 0;
    Ar << Count;
    ActiveQuests.Items.Reset();

    for (int32 i = 0; i < Count; ++i)
    {
        FName QuestIdName, StageTagName;
        Ar << QuestIdName << StageTagName;

        FGameplayTag QuestId  = FGameplayTag::RequestGameplayTag(QuestIdName,  false);
        FGameplayTag StageTag = FGameplayTag::RequestGameplayTag(StageTagName, false);

        int64 LastCompleted = 0;
        Ar << LastCompleted;

        int32 TrackerCount = 0;
        Ar << TrackerCount;

        TArray<FQuestTrackerEntry> Trackers;
        for (int32 j = 0; j < TrackerCount; ++j)
        {
            FName TrackerKeyName;
            int32 CurrentValue = 0, EffectiveTarget = 1;
            Ar << TrackerKeyName << CurrentValue << EffectiveTarget;

            FGameplayTag TrackerKey =
                FGameplayTag::RequestGameplayTag(TrackerKeyName, false);
            if (TrackerKey.IsValid())
            {
                FQuestTrackerEntry Entry;
                Entry.TrackerKey      = TrackerKey;
                Entry.CurrentValue    = CurrentValue;
                Entry.EffectiveTarget = EffectiveTarget;
                Trackers.Add(Entry);
            }
            else
            {
                UE_LOG(LogQuest, Warning,
                    TEXT("Serialize_Load: Unknown TrackerKey '%s' in quest '%s' — skipped."),
                    *TrackerKeyName.ToString(), *QuestIdName.ToString());
            }
        }

        if (!QuestId.IsValid() || !StageTag.IsValid())
        {
            UE_LOG(LogQuest, Warning,
                TEXT("Serialize_Load: Quest '%s' or stage '%s' tag unknown — skipped."),
                *QuestIdName.ToString(), *StageTagName.ToString());
            continue;
        }

        FQuestRuntime Runtime;
        Runtime.QuestId                = QuestId;
        Runtime.CurrentStageTag        = StageTag;
        Runtime.LastCompletedTimestamp = LastCompleted;
        Runtime.Trackers               = MoveTemp(Trackers);
        ActiveQuests.Items.Add(MoveTemp(Runtime));
    }

    ActiveQuests.MarkArrayDirty();
}
