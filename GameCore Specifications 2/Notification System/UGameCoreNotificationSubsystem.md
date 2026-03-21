# UGameCoreNotificationSubsystem

`ULocalPlayerSubsystem`. Single authority for notification state on a local player. Owns group storage, expiry timers, Event Bus listener handles, and delegate broadcasting. Server and non-local-player instances never get this subsystem — `ULocalPlayerSubsystem` only runs for local players.

**File:** `Notification/UGameCoreNotificationSubsystem.h`

---

## Declaration

```cpp
UCLASS()
class GAMECORE_API UGameCoreNotificationSubsystem : public ULocalPlayerSubsystem
{
    GENERATED_BODY()

public:
    // ── Delegates ────────────────────────────────────────────────────────────

    // Fired immediately after a new entry is added to a group.
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNotificationAdded,   const FNotificationEntry&, Entry);
    // Fired when a specific entry is marked viewed (individual or batch).
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNotificationViewed,  FGuid, NotificationId);
    // Fired when an entry is removed due to expiry timer elapsing.
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNotificationExpired, FGuid, NotificationId);
    // Fired after any mutation that changes a group's contents or unviewed count.
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGroupChanged,        const FNotificationGroup&, Group);
    // Fired when total unviewed count across ALL groups drops to zero.
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAllViewed);

    UPROPERTY(BlueprintAssignable, Category="Notification|Delegates")
    FOnNotificationAdded   OnNotificationAdded;

    UPROPERTY(BlueprintAssignable, Category="Notification|Delegates")
    FOnNotificationViewed  OnNotificationViewed;

    UPROPERTY(BlueprintAssignable, Category="Notification|Delegates")
    FOnNotificationExpired OnNotificationExpired;

    UPROPERTY(BlueprintAssignable, Category="Notification|Delegates")
    FOnGroupChanged        OnGroupChanged;

    UPROPERTY(BlueprintAssignable, Category="Notification|Delegates")
    FOnAllViewed           OnAllViewed;

    // ── Queries ──────────────────────────────────────────────────────────────

    // Total unviewed entries across all groups. Uses cached counter — O(1).
    UFUNCTION(BlueprintCallable, Category="Notification")
    int32 GetTotalUnviewedCount() const;

    // Unviewed entries for a specific category.
    UFUNCTION(BlueprintCallable, Category="Notification")
    int32 GetUnviewedCountForCategory(FGameplayTag CategoryTag) const;

    // Returns a copy of the group for the given category. Returns empty group if none exists.
    UFUNCTION(BlueprintCallable, Category="Notification")
    FNotificationGroup GetGroup(FGameplayTag CategoryTag) const;

    // All active groups. No guaranteed ordering — UI should sort by Entries.Last().Timestamp.
    UFUNCTION(BlueprintCallable, Category="Notification")
    TArray<FNotificationGroup> GetAllGroups() const;

    // ── Mutations ────────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category="Notification")
    void MarkViewed(FGuid NotificationId);

    UFUNCTION(BlueprintCallable, Category="Notification")
    void MarkAllViewed();

    UFUNCTION(BlueprintCallable, Category="Notification")
    void MarkCategoryViewed(FGameplayTag CategoryTag);

    // Removes a single entry from its group. Fires OnGroupChanged.
    // Clears any active expiry timer for the entry.
    UFUNCTION(BlueprintCallable, Category="Notification")
    void DismissNotification(FGuid NotificationId);

    // Removes all entries in the group for CategoryTag.
    UFUNCTION(BlueprintCallable, Category="Notification")
    void DismissCategory(FGameplayTag CategoryTag);

    // Direct push API. Bypasses Event Bus. Produces the same delegate chain as a channel-sourced push.
    // Entry.Id must be invalid (default) — the subsystem assigns a fresh FGuid.
    // Entry.CategoryTag must be valid — entries with invalid CategoryTag are silently discarded.
    UFUNCTION(BlueprintCallable, Category="Notification")
    void PushNotification(FNotificationEntry Entry);

    // Called by UNotificationChannelBinding after BuildEntry. Public to allow binding access.
    // Not intended as a game-layer API — use PushNotification for direct pushes.
    void HandleIncomingEntry(FNotificationEntry Entry);

    // ── ULocalPlayerSubsystem ────────────────────────────────────────────────
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    UPROPERTY()
    TObjectPtr<UNotificationChannelConfig>  ChannelConfig;

    UPROPERTY()
    TObjectPtr<UNotificationCategoryConfig> CategoryConfig;

    // CategoryTag → group. Source of truth for all active notifications.
    TMap<FGameplayTag, FNotificationGroup>  Groups;

    // NotificationId → expiry timer handle. Only entries with ExpirySeconds > 0 appear here.
    TMap<FGuid, FTimerHandle>               ExpiryTimers;

    // Event Bus listener handles. All unregistered in Deinitialize.
    // Stored per-binding to support per-binding unregistration.
    TArray<FGameplayMessageListenerHandle>  ListenerHandles;

    // Write-through cache of total unviewed entries across all groups.
    // Updated by every path that changes bViewed or removes an unviewed entry.
    int32 CachedTotalUnviewed = 0;

    void LoadConfig();
    void RegisterChannelListeners();
    void UnregisterChannelListeners();

    // Core push logic. Called by both PushNotification and HandleIncomingEntry.
    void PushEntryInternal(FNotificationEntry& Entry);

    void StartExpiryTimer(const FNotificationEntry& Entry);
    void ClearExpiryTimer(FGuid NotificationId);
    void OnEntryExpired(FGuid NotificationId);

    // Finds the entry and its owning group by scanning all groups.
    // Returns nullptr if not found. OutGroup is set when a non-null entry is returned.
    FNotificationEntry* FindEntry(FGuid NotificationId, FNotificationGroup** OutGroup = nullptr);

    // Returns the rule for CategoryTag, or a static default if none configured.
    const FNotificationCategoryRule& GetRule(FGameplayTag CategoryTag) const;
};
```

---

## Lifecycle

```cpp
void UGameCoreNotificationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    LoadConfig();
    RegisterChannelListeners();
}

void UGameCoreNotificationSubsystem::LoadConfig()
{
    const UGameCoreNotificationSettings* Settings = GetDefault<UGameCoreNotificationSettings>();
    if (!Settings) return;

    ChannelConfig  = Settings->ChannelConfig.LoadSynchronous();
    CategoryConfig = Settings->CategoryConfig.LoadSynchronous();
    // Null is valid — system works with PushNotification alone if no config is assigned.
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
```

---

## Channel Registration

```cpp
void UGameCoreNotificationSubsystem::RegisterChannelListeners()
{
    if (!ChannelConfig) return;

    UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this);
    if (!Bus) return;

    for (UNotificationChannelBinding* Binding : ChannelConfig->Bindings)
    {
        if (Binding && Binding->Channel.IsValid())
            Binding->RegisterListener(Bus, this, ListenerHandles);
    }
}

void UGameCoreNotificationSubsystem::UnregisterChannelListeners()
{
    UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this);
    if (!ChannelConfig || !Bus) return;

    for (UNotificationChannelBinding* Binding : ChannelConfig->Bindings)
    {
        if (Binding)
            Binding->UnregisterListeners(Bus, ListenerHandles);
    }
    ListenerHandles.Empty();
}
```

---

## `HandleIncomingEntry`

```cpp
void UGameCoreNotificationSubsystem::HandleIncomingEntry(FNotificationEntry Entry)
{
    if (!Entry.CategoryTag.IsValid())
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("UGameCoreNotificationSubsystem: binding returned entry with invalid CategoryTag — suppressed."));
        return;
    }
    PushEntryInternal(Entry);
}
```

---

## `PushEntryInternal`

```cpp
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
```

---

## `MarkViewed` / `MarkAllViewed` / `MarkCategoryViewed`

```cpp
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
```

---

## `DismissNotification` / `DismissCategory`

```cpp
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
```

---

## Expiry Timer Helpers

```cpp
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
```

---

## Helper Implementations

```cpp
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

static const FNotificationCategoryRule GDefaultCategoryRule{}; // All defaults

const FNotificationCategoryRule& UGameCoreNotificationSubsystem::GetRule(
    FGameplayTag CategoryTag) const
{
    if (CategoryConfig)
    {
        if (const FNotificationCategoryRule* Rule = CategoryConfig->FindRule(CategoryTag))
            return *Rule;
    }
    return GDefaultCategoryRule;
}

int32 UGameCoreNotificationSubsystem::GetTotalUnviewedCount() const
{
    return CachedTotalUnviewed;
}

int32 UGameCoreNotificationSubsystem::GetUnviewedCountForCategory(
    FGameplayTag CategoryTag) const
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
```

---

## Important Notes

- **`CachedTotalUnviewed` invariant**: every path that changes `bViewed` to `true` or removes an unviewed entry decrements `CachedTotalUnviewed`. Missing an update causes silent badge desync. `DismissCategory` uses `FMath::Max(0, ...)` as a safety guard.
- **`OnEntryExpired` fires before `OnGroupChanged`**: UI should respond to `OnNotificationExpired` to hide the toast, then to `OnGroupChanged` to update group badge counts. These fire in sequence from `OnEntryExpired` → `DismissNotification`.
- **`DismissNotification` pointer safety**: the `Group` pointer from `FindEntry` is used before `Groups.Remove`. The `CategoryTag` is cached locally before any mutation; `Groups.Remove` is called only after `OnGroupChanged` fires to ensure the group is still valid during the broadcast.
- **Thread safety**: game-thread only. All Event Bus callbacks and timer callbacks fire on the game thread. No locking required.
- **`GetWorld()` may return null during shutdown**: all timer operations guard with `if (UWorld* World = GetWorld())`.
