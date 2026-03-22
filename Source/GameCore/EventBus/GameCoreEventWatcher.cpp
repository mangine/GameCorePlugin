#include "GameCoreEventWatcher.h"
#include "Engine/Engine.h"

void UGameCoreEventWatcher::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UGameCoreEventBus>();
}

void UGameCoreEventWatcher::Deinitialize()
{
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
        for (auto& Pair : BusHandles)
            Bus->StopListening(Pair.Value);

    BusHandles.Empty();
    TagToHandles.Empty();
    Entries.Empty();
    Super::Deinitialize();
}

UGameCoreEventWatcher* UGameCoreEventWatcher::Get(const UObject* WorldContext)
{
    if (!WorldContext) return nullptr;
    if (UWorld* World = GEngine->GetWorldFromContextObject(
            WorldContext, EGetWorldErrorMode::ReturnNull))
    {
        return World->GetSubsystem<UGameCoreEventWatcher>();
    }
    return nullptr;
}

FEventWatchHandle UGameCoreEventWatcher::Register(
    const UObject* Owner,
    const FGameplayTagContainer& Tags,
    EGameCoreEventScope Scope,
    TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback)
{
    if (!Callback || Tags.IsEmpty()) return FEventWatchHandle{};

    FEventWatchHandle Handle{ NextHandleId++ };

    FWatchEntry Entry;
    Entry.Handle   = Handle;
    Entry.Tags     = Tags;
    Entry.Scope    = Scope;
    Entry.Callback = MoveTemp(Callback);
#if !UE_BUILD_SHIPPING
    Entry.OwnerDebugName = Owner ? Owner->GetName() : TEXT("(null)");
#endif

    Entries.Add(Handle.Id, MoveTemp(Entry));

    for (const FGameplayTag& Tag : Tags)
    {
        TagToHandles.FindOrAdd(Tag).Add(Handle.Id);
        SubscribeTagIfNeeded(Tag);
    }

    return Handle;
}

FEventWatchHandle UGameCoreEventWatcher::Register(
    const UObject* Owner, FGameplayTag Tag, EGameCoreEventScope Scope,
    TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback)
{
    FGameplayTagContainer Tags;
    Tags.AddTag(Tag);
    return Register(Owner, Tags, Scope, MoveTemp(Callback));
}

FEventWatchHandle UGameCoreEventWatcher::Register(
    const UObject* Owner, FGameplayTag Tag,
    TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback)
{
    return Register(Owner, Tag, EGameCoreEventScope::Both, MoveTemp(Callback));
}

void UGameCoreEventWatcher::Unregister(FEventWatchHandle Handle)
{
    if (!Handle.IsValid()) return;

    FWatchEntry* Entry = Entries.Find(Handle.Id);
    if (!Entry) return;

    for (const FGameplayTag& Tag : Entry->Tags)
    {
        if (TSet<uint32>* Handles = TagToHandles.Find(Tag))
        {
            Handles->Remove(Handle.Id);
            if (Handles->IsEmpty())
            {
                TagToHandles.Remove(Tag);
                UnsubscribeTagIfEmpty(Tag);
            }
        }
    }

    Entries.Remove(Handle.Id);
}

void UGameCoreEventWatcher::OnBusEvent(
    FGameplayTag Tag, const FInstancedStruct& Payload)
{
    const TSet<uint32>* HandleIds = TagToHandles.Find(Tag);
    if (!HandleIds) return;

    // Snapshot IDs — callbacks may call Register/Unregister mid-dispatch.
    TArray<uint32> IdsCopy = HandleIds->Array();

    for (uint32 Id : IdsCopy)
    {
        FWatchEntry* Entry = Entries.Find(Id);
        if (!Entry || !Entry->Callback) continue;

        if (!PassesScopeCheck(Entry->Scope)) continue;

        Entry->Callback(Tag, Payload);
    }
}

bool UGameCoreEventWatcher::PassesScopeCheck(EGameCoreEventScope Scope) const
{
    const UWorld* World = GetWorld();
    if (!World) return false;

    switch (Scope)
    {
    case EGameCoreEventScope::ServerOnly: return World->GetNetMode() != NM_Client;
    case EGameCoreEventScope::ClientOnly: return World->GetNetMode() == NM_Client
                                              || World->GetNetMode() == NM_Standalone;
    case EGameCoreEventScope::Both:       return true;
    }
    return false;
}

void UGameCoreEventWatcher::SubscribeTagIfNeeded(FGameplayTag Tag)
{
    if (BusHandles.Contains(Tag)) return;

    UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this);
    if (!Bus) return;

    FGameplayMessageListenerHandle BusHandle = Bus->StartListening(
        Tag,
        [this, Tag](FGameplayTag InTag, const FInstancedStruct& Payload)
        {
            OnBusEvent(InTag, Payload);
        });

    BusHandles.Add(Tag, BusHandle);
}

void UGameCoreEventWatcher::UnsubscribeTagIfEmpty(FGameplayTag Tag)
{
    if (FGameplayMessageListenerHandle* BusHandle = BusHandles.Find(Tag))
    {
        if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
            Bus->StopListening(*BusHandle);
        BusHandles.Remove(Tag);
    }
}
