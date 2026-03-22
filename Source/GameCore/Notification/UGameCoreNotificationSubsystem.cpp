#include "UGameCoreNotificationSubsystem.h"
#include "UGameCoreNotificationSettings.h"
#include "EventBus/GameCoreEventBus.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameCoreNotification, Log, All);

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void UGameCoreNotificationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    LoadConfig();
    RegisterChannelListeners();
}

void UGameCoreNotificationSubsystem::Deinitialize()
{
    UnregisterChannelListeners();

    if (UWorld* World = GetWorld())
    {
        for (auto& [Id, Handle] : ExpiryTimers)
            World->GetTimerManager().ClearTimer(Handle);
    }
    ExpiryTimers.Empty();
    Groups.Empty();
    CachedTotalUnviewed = 0;

    Super::Deinitialize();
}

// ── Config Loading ─────────────────────────────────────────────────────────────

void UGameCoreNotificationSubsystem::LoadConfig()
{
    const UGameCoreNotificationSettings* Settings = GetDefault<UGameCoreNotificationSettings>();
    if (!Settings) return;

    ChannelConfig  = Settings->ChannelConfig.LoadSynchronous();
    CategoryConfig = Settings->CategoryConfig.LoadSynchronous();
    // Null is valid — system works with PushNotification alone if no config is assigned.
}

// ── Channel Registration ──────────────────────────────────────────────────────

void UGameCoreNotificationSubsystem::RegisterChannelListeners()
{
    if (!ChannelConfig) return;

    UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this);
    if (!Bus) return;

    for (UNotificationChannelBinding* Binding : ChannelConfig->Bindings)
    {
        if (Binding && Binding->Channel.IsValid())
            Binding->RegisterListener(Bus, this, ListenerHandles);
    }
}

void UGameCoreNotificationSubsystem::UnregisterChannelListeners()
{
    UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this);
    if (!ChannelConfig || !Bus) return;

    for (UNotificationChannelBinding* Binding : ChannelConfig->Bindings)
    {
        if (Binding)
            Binding->UnregisterListeners(Bus, ListenerHandles);
    }
    ListenerHandles.Empty();
}

// ── Entry Handling ─────────────────────────────────────────────────────────────

void UGameCoreNotificationSubsystem::HandleIncomingEntry(FNotificationEntry Entry)
{
    if (!Entry.CategoryTag.IsValid())
    {
        UE_LOG(LogGameCoreNotification, Warning,
            TEXT("UGameCoreNotificationSubsystem: binding returned entry with invalid CategoryTag — suppressed."));
        return;
    }
    PushEntryInternal(Entry);
}

void UGameCoreNotificationSubsystem::PushNotification(FNotificationEntry Entry)
{
    if (!Entry.CategoryTag.IsValid())
    {
        UE_LOG(LogGameCoreNotification, Warning,
            TEXT("UGameCoreNotificationSubsystem::PushNotification: entry has invalid CategoryTag — suppressed."));
        return;
    }
    PushEntryInternal(Entry);
}

void UGameCoreNotificationSubsystem::PushEntryInternal(FNotificationEntry& Entry)
{
    Entry.Id        = FGuid::NewGuid();
    Entry.Timestamp = FDateTime::UtcNow();
    Entry.bViewed   = false;

    const FNotificationCategoryRule& Rule = GetRule(Entry.CategoryTag);
    FNotificationGroup& Group = Groups.FindOrAdd(Entry.CategoryTag);
    Group.CategoryTag = Entry.CategoryTag;

    // Auto-view: mark all existing entries viewed before adding the new one.
    if (Rule.bAutoViewOnStack)
    {
        for (FNotificationEntry& Existing : Group.Entries)
        {
            if (!Existing.bViewed)
            {
                Existing.bViewed = true;
                --Group.UnviewedCount;
                --CachedTotalUnviewed;
                OnNotificationViewed.Broadcast(Existing.Id);
            }
        }
    }

    // Enforce max stack count — evict oldest first (FIFO).
    if (Rule.MaxStackCount > 0)
    {
        while (Group.Entries.Num() >= Rule.MaxStackCount)
        {
            const FNotificationEntry& Evicted = Group.Entries[0];
            ClearExpiryTimer(Evicted.Id);
            if (!Evicted.bViewed)
            {
                --Group.UnviewedCount;
                --CachedTotalUnviewed;
            }
            Group.Entries.RemoveAt(0);
        }
    }

    ++Group.UnviewedCount;
    ++CachedTotalUnviewed;
    Group.Entries.Add(Entry);

    if (Entry.ExpirySeconds > 0.f)
        StartExpiryTimer(Entry);

    OnNotificationAdded.Broadcast(Entry);
    OnGroupChanged.Broadcast(Group);
}

// ── Mark Viewed ───────────────────────────────────────────────────────────────

void UGameCoreNotificationSubsystem::MarkViewed(FGuid NotificationId)
{
    FNotificationGroup* Group = nullptr;
    FNotificationEntry* Entry = FindEntry(NotificationId, &Group);
    if (!Entry || Entry->bViewed) return;

    Entry->bViewed = true;
    --Group->UnviewedCount;
    --CachedTotalUnviewed;

    OnNotificationViewed.Broadcast(NotificationId);
    OnGroupChanged.Broadcast(*Group);

    if (CachedTotalUnviewed == 0)
        OnAllViewed.Broadcast();
}

void UGameCoreNotificationSubsystem::MarkCategoryViewed(FGameplayTag CategoryTag)
{
    FNotificationGroup* Group = Groups.Find(CategoryTag);
    if (!Group) return;

    for (FNotificationEntry& Entry : Group->Entries)
    {
        if (!Entry.bViewed)
        {
            Entry.bViewed = true;
            --CachedTotalUnviewed;
            OnNotificationViewed.Broadcast(Entry.Id);
        }
    }
    Group->UnviewedCount = 0;
    OnGroupChanged.Broadcast(*Group);

    if (CachedTotalUnviewed == 0)
        OnAllViewed.Broadcast();
}

void UGameCoreNotificationSubsystem::MarkAllViewed()
{
    for (auto& [Tag, Group] : Groups)
    {
        for (FNotificationEntry& Entry : Group.Entries)
        {
            if (!Entry.bViewed)
            {
                Entry.bViewed = true;
                OnNotificationViewed.Broadcast(Entry.Id);
            }
        }
        if (Group.UnviewedCount > 0)
        {
            Group.UnviewedCount = 0;
            OnGroupChanged.Broadcast(Group);
        }
    }
    CachedTotalUnviewed = 0;
    OnAllViewed.Broadcast();
}

// ── Dismiss ───────────────────────────────────────────────────────────────────

void UGameCoreNotificationSubsystem::DismissNotification(FGuid NotificationId)
{
    FNotificationGroup* Group = nullptr;
    FNotificationEntry* Entry = FindEntry(NotificationId, &Group);
    if (!Entry) return;

    // Cache tag before any mutation — Group pointer may be invalidated by Remove.
    FGameplayTag CategoryTag = Group->CategoryTag;

    ClearExpiryTimer(NotificationId);

    const bool bWasUnviewed = !Entry->bViewed;
    Group->Entries.RemoveAll([NotificationId](const FNotificationEntry& E)
        { return E.Id == NotificationId; });

    if (bWasUnviewed)
    {
        --Group->UnviewedCount;
        --CachedTotalUnviewed;
    }

    // Fire OnGroupChanged before potentially removing the group.
    OnGroupChanged.Broadcast(*Group);

    if (Group->Entries.IsEmpty())
        Groups.Remove(CategoryTag);

    if (CachedTotalUnviewed == 0)
        OnAllViewed.Broadcast();
}

void UGameCoreNotificationSubsystem::DismissCategory(FGameplayTag CategoryTag)
{
    FNotificationGroup* Group = Groups.Find(CategoryTag);
    if (!Group) return;

    for (const FNotificationEntry& Entry : Group->Entries)
        ClearExpiryTimer(Entry.Id);

    CachedTotalUnviewed -= Group->UnviewedCount;
    // Ensure no underflow from misconfiguration.
    CachedTotalUnviewed = FMath::Max(0, CachedTotalUnviewed);

    Groups.Remove(CategoryTag);

    if (CachedTotalUnviewed == 0)
        OnAllViewed.Broadcast();
}

// ── Expiry Timers ─────────────────────────────────────────────────────────────

void UGameCoreNotificationSubsystem::StartExpiryTimer(const FNotificationEntry& Entry)
{
    UWorld* World = GetWorld();
    if (!World || Entry.ExpirySeconds <= 0.f) return;

    FTimerHandle Handle;
    const FGuid Id = Entry.Id;
    World->GetTimerManager().SetTimer(
        Handle,
        FTimerDelegate::CreateUObject(
            this, &UGameCoreNotificationSubsystem::OnEntryExpired, Id),
        Entry.ExpirySeconds,
        false);

    ExpiryTimers.Add(Id, Handle);
}

void UGameCoreNotificationSubsystem::ClearExpiryTimer(FGuid NotificationId)
{
    if (FTimerHandle* Handle = ExpiryTimers.Find(NotificationId))
    {
        if (UWorld* World = GetWorld())
            World->GetTimerManager().ClearTimer(*Handle);
        ExpiryTimers.Remove(NotificationId);
    }
}

void UGameCoreNotificationSubsystem::OnEntryExpired(FGuid NotificationId)
{
    // Remove from map first — the handle is stale after firing.
    // ClearExpiryTimer called by DismissNotification will find nothing and return cleanly.
    ExpiryTimers.Remove(NotificationId);
    OnNotificationExpired.Broadcast(NotificationId);
    DismissNotification(NotificationId); // Cleans up Groups + fires OnGroupChanged.
}

// ── Query Helpers ─────────────────────────────────────────────────────────────

int32 UGameCoreNotificationSubsystem::GetTotalUnviewedCount() const
{
    return CachedTotalUnviewed;
}

int32 UGameCoreNotificationSubsystem::GetUnviewedCountForCategory(FGameplayTag CategoryTag) const
{
    if (const FNotificationGroup* Group = Groups.Find(CategoryTag))
        return Group->UnviewedCount;
    return 0;
}

FNotificationGroup UGameCoreNotificationSubsystem::GetGroup(FGameplayTag CategoryTag) const
{
    if (const FNotificationGroup* Group = Groups.Find(CategoryTag))
        return *Group;
    return FNotificationGroup{};
}

TArray<FNotificationGroup> UGameCoreNotificationSubsystem::GetAllGroups() const
{
    TArray<FNotificationGroup> Result;
    Result.Reserve(Groups.Num());
    for (const auto& [Tag, Group] : Groups)
        Result.Add(Group);
    return Result;
}

// ── Private Helpers ───────────────────────────────────────────────────────────

FNotificationEntry* UGameCoreNotificationSubsystem::FindEntry(
    FGuid NotificationId, FNotificationGroup** OutGroup)
{
    for (auto& [Tag, Group] : Groups)
    {
        for (FNotificationEntry& Entry : Group.Entries)
        {
            if (Entry.Id == NotificationId)
            {
                if (OutGroup) *OutGroup = &Group;
                return &Entry;
            }
        }
    }
    return nullptr;
}

static const FNotificationCategoryRule GDefaultCategoryRule{}; // All defaults: MaxStackCount=0, bAutoViewOnStack=false

const FNotificationCategoryRule& UGameCoreNotificationSubsystem::GetRule(FGameplayTag CategoryTag) const
{
    if (CategoryConfig)
    {
        if (const FNotificationCategoryRule* Rule = CategoryConfig->FindRule(CategoryTag))
            return *Rule;
    }
    return GDefaultCategoryRule;
}
