#include "Quest/Subsystems/QuestRegistrySubsystem.h"
#include "Quest/Data/QuestDefinition.h"
#include "Quest/Components/QuestComponent.h"
#include "Quest/Events/QuestEventPayloads.h"
#include "EventBus/GameCoreEventBus.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerState.h"

DEFINE_LOG_CATEGORY_STATIC(LogQuestRegistry, Log, All);

// ---------------------------------------------------------------------------
// Initialize / Deinitialize
// ---------------------------------------------------------------------------

void UQuestRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UAssetManager& AM = UAssetManager::Get();
    AM.GetPrimaryAssetIdList(FPrimaryAssetType(TEXT("QuestDefinition")), AllQuestAssetIds);

    // Pre-build leaf-name lookup map for O(1) path resolution.
    for (const FPrimaryAssetId& AssetId : AllQuestAssetIds)
        QuestAssetIdByLeafName.Add(AssetId.PrimaryAssetName, AssetId);

    ComputeResetTimestamps();

    // Cadence clock runs server-side only.
    const UWorld* World = GetGameInstance()->GetWorld();
    if (World && World->GetNetMode() < NM_Client)
    {
        GetGameInstance()->GetTimerManager().SetTimer(
            CadenceCheckTimer, this,
            &UQuestRegistrySubsystem::TickCadenceCheck,
            60.0f, true);
    }
}

void UQuestRegistrySubsystem::Deinitialize()
{
    GetGameInstance()->GetTimerManager().ClearTimer(CadenceCheckTimer);
    LoadedDefinitions.Empty();
    Super::Deinitialize();
}

// ---------------------------------------------------------------------------
// Definition Lookup
// ---------------------------------------------------------------------------

const UQuestDefinition* UQuestRegistrySubsystem::GetDefinition(
    const FGameplayTag& QuestId) const
{
    const FQuestLoadState* State = LoadedDefinitions.Find(QuestId);
    return State ? State->Definition.Get() : nullptr;
}

void UQuestRegistrySubsystem::GetOrLoadDefinitionAsync(
    const FGameplayTag& QuestId,
    TFunction<void(const UQuestDefinition*)> OnLoaded)
{
    FQuestLoadState& State = LoadedDefinitions.FindOrAdd(QuestId);

    if (State.Definition)
    {
        OnLoaded(State.Definition);
        return;
    }

    State.PendingCallbacks.Add(MoveTemp(OnLoaded));
    if (State.LoadHandle) return; // load already in-flight

    const FSoftObjectPath AssetPath = ResolveQuestPath(QuestId);
    if (!AssetPath.IsValid())
    {
        for (auto& CB : State.PendingCallbacks) CB(nullptr);
        State.PendingCallbacks.Empty();
        return;
    }

    State.LoadHandle = UAssetManager::Get().GetStreamableManager().RequestAsyncLoad(
        AssetPath,
        [this, QuestId]()
        {
            FQuestLoadState& S = LoadedDefinitions[QuestId];
            S.Definition = Cast<UQuestDefinition>(S.LoadHandle->GetLoadedAsset());
            for (auto& CB : S.PendingCallbacks) CB(S.Definition);
            S.PendingCallbacks.Empty();
        });
}

// ---------------------------------------------------------------------------
// Reference Counting
// ---------------------------------------------------------------------------

void UQuestRegistrySubsystem::AddReference(const FGameplayTag& QuestId)
{
    if (FQuestLoadState* State = LoadedDefinitions.Find(QuestId))
        ++State->RefCount;
}

void UQuestRegistrySubsystem::ReleaseReference(const FGameplayTag& QuestId)
{
    FQuestLoadState* State = LoadedDefinitions.Find(QuestId);
    if (!State) return;
    if (--State->RefCount <= 0)
    {
        State->LoadHandle.Reset();
        State->Definition = nullptr;
        State->RefCount   = 0;
    }
}

// ---------------------------------------------------------------------------
// Iteration
// ---------------------------------------------------------------------------

void UQuestRegistrySubsystem::IterateAllDefinitions(
    TFunctionRef<void(const FGameplayTag&, const UQuestDefinition*)> Visitor) const
{
    for (const auto& Pair : LoadedDefinitions)
    {
        if (Pair.Value.Definition)
            Visitor(Pair.Key, Pair.Value.Definition);
    }
}

// ---------------------------------------------------------------------------
// Path Resolution (O(1) tag-to-asset)
// ---------------------------------------------------------------------------

FSoftObjectPath UQuestRegistrySubsystem::ResolveQuestPath(
    const FGameplayTag& QuestId) const
{
    const FString TagStr = QuestId.GetTagName().ToString();
    const int32 LastDot  = TagStr.Find(TEXT("."), ESearchCase::IgnoreCase,
                                        ESearchDir::FromEnd);
    const FName LeafName = FName(*TagStr.RightChop(LastDot + 1));

    const FPrimaryAssetId* AssetId = QuestAssetIdByLeafName.Find(LeafName);
    if (!AssetId)
    {
        UE_LOG(LogQuestRegistry, Warning,
            TEXT("ResolveQuestPath: No asset for QuestId '%s'."),
            *QuestId.ToString());
        return FSoftObjectPath();
    }

    FSoftObjectPath Path;
    UAssetManager::Get().GetPrimaryAssetPath(*AssetId, Path);
    return Path;
}

// ---------------------------------------------------------------------------
// Cadence Clock
// ---------------------------------------------------------------------------

int64 UQuestRegistrySubsystem::ComputeLastDailyReset(int64 NowTs)
{
    // Floor to midnight UTC
    return (NowTs / 86400LL) * 86400LL;
}

int64 UQuestRegistrySubsystem::ComputeLastWeeklyReset(int64 NowTs)
{
    // Monday 00:00 UTC. Epoch (1970-01-01) was a Thursday.
    // Days since epoch: offset by 3 to align Thursday=0 → Monday=0 → floor
    const int64 DaysSinceEpoch = NowTs / 86400LL;
    const int64 DaysSinceMonday = (DaysSinceEpoch + 3LL) % 7LL;
    return (DaysSinceEpoch - DaysSinceMonday) * 86400LL;
}

void UQuestRegistrySubsystem::ComputeResetTimestamps()
{
    const int64 Now       = FDateTime::UtcNow().ToUnixTimestamp();
    LastDailyResetTimestamp  = ComputeLastDailyReset(Now);
    LastWeeklyResetTimestamp = ComputeLastWeeklyReset(Now);
}

void UQuestRegistrySubsystem::TickCadenceCheck()
{
    const int64 Now       = FDateTime::UtcNow().ToUnixTimestamp();
    const int64 NewDaily  = ComputeLastDailyReset(Now);
    const int64 NewWeekly = ComputeLastWeeklyReset(Now);

    if (NewDaily > LastDailyResetTimestamp)
    {
        LastDailyResetTimestamp = NewDaily;
        OnDailyReset();
    }
    if (NewWeekly > LastWeeklyResetTimestamp)
    {
        LastWeeklyResetTimestamp = NewWeekly;
        OnWeeklyReset();
    }
}

void UQuestRegistrySubsystem::OnDailyReset()
{
    FlushCadenceResetsForAllPlayers(EQuestResetCadence::Daily);

    FQuestResetPayload Payload;
    Payload.Cadence = EQuestResetCadence::Daily;
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(GetGameInstance()))
        Bus->Broadcast(TAG_GameCoreEvent_Quest_DailyReset, Payload);
}

void UQuestRegistrySubsystem::OnWeeklyReset()
{
    FlushCadenceResetsForAllPlayers(EQuestResetCadence::Weekly);

    FQuestResetPayload Payload;
    Payload.Cadence = EQuestResetCadence::Weekly;
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(GetGameInstance()))
        Bus->Broadcast(TAG_GameCoreEvent_Quest_WeeklyReset, Payload);
}

void UQuestRegistrySubsystem::FlushCadenceResetsForAllPlayers(
    EQuestResetCadence Cadence)
{
    for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
    {
        UWorld* World = Ctx.World();
        if (!World || World->GetNetMode() > NM_ListenServer) continue;

        for (APlayerState* PS : TActorRange<APlayerState>(World))
        {
            if (UQuestComponent* QC =
                PS->FindComponentByClass<UQuestComponent>())
            {
                QC->FlushCadenceResets(Cadence);
            }
        }
    }
}
