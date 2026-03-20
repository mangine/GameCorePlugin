# UGameCoreEventWatcher

**Sub-page of:** [Event Bus System](../Event%20Bus%20System.md)

`UGameCoreEventWatcher` is a generic `UWorldSubsystem` that bridges the event bus to registered callbacks. Any system can register a delegate against one or more `FGameplayTag` channels. When a matching event arrives on the bus, the registered callback is called immediately with the raw `FInstancedStruct` payload.

This subsystem owns no domain knowledge. It does not know about requirements, quests, or any other system. It is a routing layer — subscribe, receive, call back.

**File:** `EventBus/GameCoreEventWatcher.h / .cpp`

---

# Design Principles

- **Raw callbacks only.** Every registered callback receives `FInstancedStruct` directly. Domain-specific behaviour (requirement evaluation, filtering, context injection) is the caller's responsibility — implemented in a closure at registration time.
- **No coalescing.** Events are delivered immediately and synchronously as they arrive on the bus. Each registration receives one call per broadcast. If a system needs its own coalescing, it owns that logic.
- **Lazy bus subscription.** The watcher subscribes to a given tag on `UGameCoreEventBus` the first time any caller registers for it, and unsubscribes when the last registration for that tag is removed. No standing subscription to a parent tag.
- **Handle-based lifetime.** Every registration returns an `FEventWatchHandle`. The caller stores the handle and calls `Unregister(Handle)` at teardown. The watcher never assumes lifetime.
- **Closure carries caller context.** The caller captures any private state (quest ID, component pointer, etc.) in the lambda at registration time. The watcher stores `TFunction<void(FGameplayTag, const FInstancedStruct&)>` and knows nothing about the captured data.

---

# `FEventWatchHandle`

```cpp
struct GAMECORE_API FEventWatchHandle
{
    uint32 Id = 0;
    bool IsValid() const { return Id != 0; }
    bool operator==(const FEventWatchHandle& Other) const { return Id == Other.Id; }
    bool operator!=(const FEventWatchHandle& Other) const { return Id != Other.Id; }
};

inline uint32 GetTypeHash(const FEventWatchHandle& H) { return H.Id; }
```

Handle `0` is always invalid. IDs are monotonically increasing per subsystem instance.

---

# Class Definition

```cpp
UCLASS()
class GAMECORE_API UGameCoreEventWatcher : public UWorldSubsystem
{
    GENERATED_BODY()
public:

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Static accessor — consistent with UGameCoreEventBus::Get.
    static UGameCoreEventWatcher* Get(const UObject* WorldContext);

    // ── Registration ─────────────────────────────────────────────────────

    // Register a callback for one or more event tags.
    // The callback fires immediately (synchronously) when a matching event
    // arrives on the bus, once per broadcast, with the raw payload.
    //
    // Owner is used for safety logging only — the watcher does not manage
    // the owner's lifetime. Use TWeakObjectPtr captures inside the callback
    // if the owner may be destroyed before Unregister is called.
    //
    // Returns an FEventWatchHandle. Store it and pass to Unregister at teardown.
    FEventWatchHandle Register(
        const UObject* Owner,
        const FGameplayTagContainer& Tags,
        TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback);

    // Convenience overload for a single tag.
    FEventWatchHandle Register(
        const UObject* Owner,
        FGameplayTag Tag,
        TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback);

    // Removes the registration associated with Handle.
    // Unsubscribes from the bus if this was the last registration for a given tag.
    // Safe to call with an invalid handle.
    void Unregister(FEventWatchHandle Handle);

private:

    struct FWatchEntry
    {
        FEventWatchHandle Handle;
        FGameplayTagContainer Tags;
        TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback;
#if !UE_BUILD_SHIPPING
        FString OwnerDebugName; // logged if callback fires after owner is gone
#endif
    };

    // All registered entries, keyed by handle ID.
    TMap<uint32, FWatchEntry> Entries;

    // Tag → set of handle IDs watching it. Built/updated at Register time.
    TMap<FGameplayTag, TSet<uint32>> TagToHandles;

    // One bus listener handle per actively-subscribed tag.
    TMap<FGameplayTag, FGameplayMessageListenerHandle> BusHandles;

    uint32 NextHandleId = 1;

    void SubscribeTagIfNeeded(FGameplayTag Tag);
    void UnsubscribeTagIfEmpty(FGameplayTag Tag);
    void OnBusEvent(FGameplayTag Tag, const FInstancedStruct& Payload);
};
```

---

# Implementation

## `Register`

```cpp
FEventWatchHandle UGameCoreEventWatcher::Register(
    const UObject* Owner,
    const FGameplayTagContainer& Tags,
    TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback)
{
    if (!Callback || Tags.IsEmpty()) return FEventWatchHandle{};

    FEventWatchHandle Handle{ NextHandleId++ };

    FWatchEntry Entry;
    Entry.Handle   = Handle;
    Entry.Tags     = Tags;
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
    const UObject* Owner,
    FGameplayTag Tag,
    TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback)
{
    FGameplayTagContainer Tags;
    Tags.AddTag(Tag);
    return Register(Owner, Tags, MoveTemp(Callback));
}
```

## `Unregister`

```cpp
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
```

## Lazy bus subscription

```cpp
void UGameCoreEventWatcher::SubscribeTagIfNeeded(FGameplayTag Tag)
{
    if (BusHandles.Contains(Tag)) return;

    UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this);
    if (!Bus) return;

    FGameplayMessageListenerHandle BusHandle = Bus->StartListening(
        Tag, this,
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
        UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this);
        if (Bus) Bus->StopListening(*BusHandle);
        BusHandles.Remove(Tag);
    }
}
```

## Event dispatch

```cpp
void UGameCoreEventWatcher::OnBusEvent(
    FGameplayTag Tag, const FInstancedStruct& Payload)
{
    const TSet<uint32>* HandleIds = TagToHandles.Find(Tag);
    if (!HandleIds) return;

    // Copy handle IDs — callbacks may call Unregister, modifying TagToHandles.
    TArray<uint32> IdsCopy = HandleIds->Array();

    for (uint32 Id : IdsCopy)
    {
        FWatchEntry* Entry = Entries.Find(Id);
        if (Entry && Entry->Callback)
            Entry->Callback(Tag, Payload);
    }
}
```

**Re-entrancy note.** `OnBusEvent` copies handle IDs before iterating so that a callback which calls `Unregister` or `Register` mid-dispatch does not corrupt the active iterator. New registrations during dispatch take effect on the next event; unregistrations during dispatch are safe.

## `Deinitialize`

```cpp
void UGameCoreEventWatcher::Deinitialize()
{
    UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this);
    if (Bus)
    {
        for (auto& Pair : BusHandles)
            Bus->StopListening(Pair.Value);
    }
    BusHandles.Empty();
    TagToHandles.Empty();
    Entries.Empty();
    Super::Deinitialize();
}
```

---

# Usage

## Basic registration

```cpp
// At setup — capture caller context in the closure.
FGameplayTag LevelTag =
    FGameplayTag::RequestGameplayTag("RequirementEvent.Leveling.LevelChanged");

TWeakObjectPtr<UMySystem> WeakThis = this;
FMyPrivateData CapturedData = MyData;

WatchHandle = Watcher->Register(this, LevelTag,
    [WeakThis, CapturedData](FGameplayTag Tag, const FInstancedStruct& Payload)
    {
        if (UMySystem* Self = WeakThis.Get())
            Self->OnLevelEvent(CapturedData, Payload);
    });

// At teardown.
Watcher->Unregister(WatchHandle);
```

## Multiple tags in one registration

```cpp
FGameplayTagContainer Tags;
Tags.AddTag(FGameplayTag::RequestGameplayTag("RequirementEvent.Inventory.ItemAdded"));
Tags.AddTag(FGameplayTag::RequestGameplayTag("RequirementEvent.Inventory.ItemRemoved"));

WatchHandle = Watcher->Register(this, Tags,
    [WeakThis](FGameplayTag Tag, const FInstancedStruct& Payload)
    {
        if (UMySystem* Self = WeakThis.Get())
            Self->OnInventoryEvent(Tag, Payload);
    });
```

One handle covers all tags. `Unregister(Handle)` removes the callback from all of them.

---

# Known Limitations

- **No parent tag subscription.** Inherits the GMS exact-match constraint. Register leaf tags explicitly.
- **No built-in coalescing.** Each broadcast triggers immediate dispatch. Systems that need coalescing own that logic themselves.
- **Callback fires synchronously.** GMS is synchronous. Heavy work in a callback blocks the broadcast. Defer expensive logic via a timer or game thread task.
- **Re-entrancy copies IDs.** Safe but means a `Register` call inside a callback does not receive the current event — it takes effect from the next broadcast.
