# `UGameCoreNotificationSubsystem`

**Sub-page of:** [Notification System Overview](Notification%20System%20Overview.md)

---

## Class Definition

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
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNotificationViewed,  FGuid,                     NotificationId);
    // Fired when an entry is removed due to expiry timer elapsing.
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNotificationExpired, FGuid,                     NotificationId);
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

    // Total unviewed entries across all groups.
    UFUNCTION(BlueprintCallable, Category="Notification")
    int32 GetTotalUnviewedCount() const;

    // Unviewed entries for a specific category.
    UFUNCTION(BlueprintCallable, Category="Notification")
    int32 GetUnviewedCountForCategory(FGameplayTag CategoryTag) const;

    // Returns a copy of the group for the given category. Returns empty group if none.
    UFUNCTION(BlueprintCallable, Category="Notification")
    FNotificationGroup GetGroup(FGameplayTag CategoryTag) const;

    // All active groups. Ordered by time of last push per group (most recent last).
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
    // If entry had expiry timer, timer is cleared.
    UFUNCTION(BlueprintCallable, Category="Notification")
    void DismissNotification(FGuid NotificationId);

    // Removes all entries in the group for CategoryTag.
    UFUNCTION(BlueprintCallable, Category="Notification")
    void DismissCategory(FGameplayTag CategoryTag);

    // Direct push. Bypasses GMS. Produces the same delegate chain as a GMS-sourced push.
    // Entry.Id must be invalid (default) — the subsystem assigns a fresh FGuid.
    // Entry.CategoryTag must be valid.
    UFUNCTION(BlueprintCallable, Category="Notification")
    void PushNotification(FNotificationEntry Entry);

    // ── ULocalPlayerSubsystem ────────────────────────────────────────────────
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    // Loaded from UGameCoreNotificationSettings at Initialize.
    UPROPERTY()
    TObjectPtr<UNotificationChannelConfig>  ChannelConfig;

    UPROPERTY()
    TObjectPtr<UNotificationCategoryConfig> CategoryConfig;

    // CategoryTag → group. Source of truth for all active notifications.
    TMap<FGameplayTag, FNotificationGroup>  Groups;

    // NotificationId → expiry timer handle. Only entries with ExpirySeconds > 0 appear here.
    TMap<FGuid, FTimerHandle>               ExpiryTimers;

    // GMS listener handles. All unregistered in Deinitialize.
    TArray<FGameplayMessageListenerHandle>  ListenerHandles;

    // Cache total unviewed to avoid iterating all groups on every query.
    int32 CachedTotalUnviewed = 0;

    // ── Internal helpers ─────────────────────────────────────────────────────
    void LoadConfig();
    void RegisterChannelListeners();
    void UnregisterChannelListeners();

    // Called by each GMS listener closure.
    void HandleIncomingEntry(FNotificationEntry Entry);

    // Core push logic. Called by both PushNotification and HandleIncomingEntry.
    void PushEntryInternal(FNotificationEntry& Entry);

    // Starts a timer for Entry if ExpirySeconds > 0.
    void StartExpiryTimer(const FNotificationEntry& Entry);
    void ClearExpiryTimer(FGuid NotificationId);

    // Called by timer. Removes entry and fires OnNotificationExpired.
    void OnEntryExpired(FGuid NotificationId);

    // Finds the entry and its owning group pointer. Returns nullptr if not found.
    FNotificationEntry* FindEntry(FGuid NotificationId, FNotificationGroup** OutGroup = nullptr);

    // Returns the rule for CategoryTag from CategoryConfig, or a static default.
    const FNotificationCategoryRule& GetRule(FGameplayTag CategoryTag) const;
};
```

---

## Lifecycle

### `Initialize`

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
    // Null is valid — system works with direct PushNotification calls even without config.
}
```

### `Deinitialize`

```cpp
void UGameCoreNotificationSubsystem::Deinitialize()
{
    UnregisterChannelListeners();

    // Clear all pending expiry timers.
    if (UWorld* World = GetWorld())
    {
        for (auto& [Id, Handle] : ExpiryTimers)
        {
            World->GetTimerManager().ClearTimer(Handle);
        }
    }
    ExpiryTimers.Empty();
    Groups.Empty();

    Super::Deinitialize();
}
```

---

## GMS Channel Registration

```cpp
void UGameCoreNotificationSubsystem::RegisterChannelListeners()
{
    if (!ChannelConfig) return;

    UGameplayMessageSubsystem* GMS =
        GetWorld() ? GetWorld()->GetSubsystem<UGameplayMessageSubsystem>() : nullptr;
    if (!GMS) return;

    for (UNotificationChannelBinding* Binding : ChannelConfig->Bindings)
    {
        if (!Binding || !Binding->Channel.IsValid()) continue;

        // We register a wildcard FStructView listener per channel.
        // The binding's BuildEntry is called with just the channel tag —
        // C++ bindings capture the typed payload via the closure below.
        FGameplayMessageListenerHandle Handle =
            GMS->RegisterListener<FGameplayTag>(
                Binding->Channel,
                [this, Binding](FGameplayTag Channel, const FGameplayTag& /*unused*/)
                {
                    FNotificationEntry Entry = Binding->BuildEntry(Channel);
                    if (Entry.Id.IsValid()) return; // Binding suppressed
                    HandleIncomingEntry(Entry);
                });

        ListenerHandles.Add(Handle);
    }
}
```

> See [GMS Integration](GMS%20Integration.md) for the correct C++ pattern for typed payloads, which uses a different approach than the above lambda (the above illustrates the wiring flow; typed C++ bindings override `BuildEntry` and carry their typed listener separately).

---

## `PushEntryInternal`

Core logic called by both `PushNotification` and `HandleIncomingEntry`.

```cpp
void UGameCoreNotificationSubsystem::PushEntryInternal(FNotificationEntry& Entry)
{
    // Assign ID and timestamp.
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

    // Enforce max stack count — evict oldest first.
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

    // Add entry and update unviewed count.
    ++Group.UnviewedCount;
    ++CachedTotalUnviewed;
    Group.Entries.Add(Entry);

    // Start expiry if needed.
    if (Entry.ExpirySeconds > 0.f)
        StartExpiryTimer(Entry);

    // Fire delegates.
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

    ClearExpiryTimer(NotificationId);

    if (!Entry->bViewed)
    {
        --Group->UnviewedCount;
        --CachedTotalUnviewed;
    }
    Group->Entries.RemoveAll([&](const FNotificationEntry& E){ return E.Id == NotificationId; });
    OnGroupChanged.Broadcast(*Group);

    if (Group->Entries.IsEmpty())
        Groups.Remove(Group->CategoryTag);

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
    FGuid Id = Entry.Id;
    World->GetTimerManager().SetTimer(
        Handle,
        FTimerDelegate::CreateUObject(this, &UGameCoreNotificationSubsystem::OnEntryExpired, Id),
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
    ExpiryTimers.Remove(NotificationId); // Timer already fired, handle is stale.
    OnNotificationExpired.Broadcast(NotificationId);
    DismissNotification(NotificationId); // Cleans up Groups and fires OnGroupChanged.
}
```

---

## `FindEntry` Helper

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
```

---

## `GetRule` Helper

```cpp
static const FNotificationCategoryRule GDefaultRule{}; // All defaults: 0 max, no format, no auto-view

const FNotificationCategoryRule& UGameCoreNotificationSubsystem::GetRule(FGameplayTag CategoryTag) const
{
    if (CategoryConfig)
    {
        if (const FNotificationCategoryRule* Rule = CategoryConfig->FindRule(CategoryTag))
            return *Rule;
    }
    return GDefaultRule;
}
```

---

## Important Notes

- **`CachedTotalUnviewed`** is a write-through counter. Every path that changes `bViewed` or removes an unviewed entry must update both the group's `UnviewedCount` AND the cache. Missing an update causes a badge count desync that is difficult to debug. Always use the internal helpers.
- **`OnEntryExpired` fires before `OnGroupChanged`** from `DismissNotification`. UI should handle both in sequence: hide the toast on `OnNotificationExpired`, then update group badge on `OnGroupChanged`.
- **`ULocalPlayerSubsystem::GetWorld()`** may return null during shutdown. All timer operations guard with `if (UWorld* World = GetWorld())`.
- **Thread safety**: This subsystem is game-thread only. GMS callbacks and timer callbacks all fire on the game thread. No locking required.
