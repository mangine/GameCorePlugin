#include "GameCoreEventBus.h"
#include "Engine/Engine.h"

void UGameCoreEventBus::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UGameplayMessageSubsystem>();
    GMS = GetWorld()->GetSubsystem<UGameplayMessageSubsystem>();
    check(GMS);

    GetWorld()->GetTimerManager().SetTimer(
        ExpiryTimerHandle,
        this, &UGameCoreEventBus::SweepExpiredEvents,
        30.f, true);
}

void UGameCoreEventBus::Deinitialize()
{
    GetWorld()->GetTimerManager().ClearTimer(ExpiryTimerHandle);
    ActiveEventRegistry.Reset();
    GMS = nullptr;
    Super::Deinitialize();
}

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

void UGameCoreEventBus::StopListening(FGameplayMessageListenerHandle& Handle)
{
    if (GMS && Handle.IsValid())
    {
        GMS->UnregisterListener(Handle);
        Handle = FGameplayMessageListenerHandle{};
    }
}

FGuid UGameCoreEventBus::RegisterActiveEvent(
    FGameplayTag EventTag, float Duration, UObject* Instigator)
{
    FGuid Id = FGuid::NewGuid();
    FActiveEventRecord& Record     = ActiveEventRegistry.Add(Id);
    Record.EventId                 = Id;
    Record.EventTag                = EventTag;
    Record.RegisteredAtSeconds     = FPlatformTime::Seconds();
    Record.ExpectedDuration        = Duration;
    Record.Instigator              = Instigator;
    return Id;
}

void UGameCoreEventBus::UnregisterActiveEvent(FGuid EventId)
{
    ActiveEventRegistry.Remove(EventId);
}

bool UGameCoreEventBus::IsEventActive(FGameplayTag EventTag) const
{
    for (const auto& [Id, Record] : ActiveEventRegistry)
        if (Record.EventTag.MatchesTag(EventTag))
            return true;
    return false;
}

TArray<FGuid> UGameCoreEventBus::GetActiveEvents(FGameplayTag CategoryTag) const
{
    TArray<FGuid> Result;
    for (const auto& [Id, Record] : ActiveEventRegistry)
        if (Record.EventTag.MatchesTag(CategoryTag))
            Result.Add(Id);
    return Result;
}

void UGameCoreEventBus::SweepExpiredEvents()
{
    double Now = FPlatformTime::Seconds();
    TArray<FGuid> Expired;
    for (const auto& [Id, Record] : ActiveEventRegistry)
    {
        if (Record.ExpectedDuration > 0.f &&
            (Now - Record.RegisteredAtSeconds) > Record.ExpectedDuration + 30.0)
            Expired.Add(Id);
    }
    for (FGuid Id : Expired)
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("ActiveEventRegistry: Auto-expiring stale event [%s]"),
            *ActiveEventRegistry[Id].EventTag.ToString());
        ActiveEventRegistry.Remove(Id);
    }
}

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
