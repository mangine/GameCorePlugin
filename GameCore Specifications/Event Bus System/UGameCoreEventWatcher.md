# UGameCoreEventWatcher

**Sub-page of:** [Event Bus System](../Event%20Bus%20System.md)

`UGameCoreEventWatcher` is a generic `UWorldSubsystem` that bridges the event bus to registered callbacks. Any system can register a delegate against one or more `FGameplayTag` channels. When a matching event arrives on the bus, the registered callback is called immediately with the raw `FInstancedStruct` payload.

This subsystem owns no domain knowledge. It does not know about requirements, quests, or any other system. It is a routing layer — subscribe, receive, enforce scope, call back.

**File:** `EventBus/GameCoreEventWatcher.h / .cpp`

---

# Design Principles

- **Raw callbacks only.** Every registered callback receives `FInstancedStruct` directly. Domain-specific behaviour is the caller's responsibility — implemented in a closure at registration time.
- **Scope enforcement at delivery.** Registrations declare `EGameCoreEventScope`. The watcher checks net role before invoking the callback. A `ServerOnly` registration never fires on a client, even if the event was broadcast with `Both` scope.
- **No coalescing.** Events are delivered immediately and synchronously. Each registration receives one call per broadcast. Systems that need coalescing own that logic.
- **Lazy bus subscription.** The watcher subscribes to a tag on `UGameCoreEventBus` the first time any caller registers for it, and unsubscribes when the last registration for that tag is removed.
- **Handle-based lifetime.** Every registration returns an `FEventWatchHandle`. The caller stores the handle and calls `Unregister(Handle)` at teardown.
- **Closure carries caller context.** The caller captures any private state in the lambda. The watcher stores `TFunction<void(FGameplayTag, const FInstancedStruct&)>` and knows nothing about captured data.

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

    static UGameCoreEventWatcher* Get(const UObject* WorldContext);

    // ── Registration ─────────────────────────────────────────────────────

    // Register a callback for one or more event tags.
    //
    // Scope controls which network side this registration is active on.
    // The callback is silently skipped if the current net role does not match:
    //   ServerOnly  — only fires on server (NM_DedicatedServer, NM_ListenServer)
    //   ClientOnly  — only fires on client (NM_Client) and standalone
    //   Both        — fires on all sides
    //
    // Owner is used for debug logging only. Use TWeakObjectPtr captures in the
    // callback for lifetime safety — the watcher does not manage owner lifetime.
    //
    // Returns FEventWatchHandle. Store it and pass to Unregister at teardown.
    FEventWatchHandle Register(
        const UObject* Owner,
        const FGameplayTagContainer& Tags,
        EGameCoreEventScope Scope,
        TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback);

    // Convenience overload for a single tag.
    FEventWatchHandle Register(
        const UObject* Owner,
        FGameplayTag Tag,
        EGameCoreEventScope Scope,
        TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback);

    // Scope defaults to Both for callers that do not need enforcement.
    FEventWatchHandle Register(
        const UObject* Owner,
        FGameplayTag Tag,
        TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback);

    // Removes the registration. Unsubscribes from bus if last for that tag.
    // Safe to call with an invalid handle.
    void Unregister(FEventWatchHandle Handle);

private:

    struct FWatchEntry
    {
        FEventWatchHandle Handle;
        FGameplayTagContainer Tags;
        EGameCoreEventScope Scope = EGameCoreEventScope::Both;
        TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback;
#if !UE_BUILD_SHIPPING
        FString OwnerDebugName;
#endif
    };

    TMap<uint32, FWatchEntry> Entries;
    TMap<FGameplayTag, TSet<uint32>> TagToHandles;
    TMap<FGameplayTag, FGameplayMessageListenerHandle> BusHandles;
    uint32 NextHandleId = 1;

    void SubscribeTagIfNeeded(FGameplayTag Tag);
    void UnsubscribeTagIfEmpty(FGameplayTag Tag);
    void OnBusEvent(FGameplayTag Tag, const FInstancedStruct& Payload);
    bool PassesScopeCheck(EGameCoreEventScope Scope) const;
};
```

---

# Implementation

## `Register`

```cpp
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

// Single-tag overloads:
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

## Scope enforcement in `OnBusEvent`

```cpp
void UGameCoreEventWatcher::OnBusEvent(
    FGameplayTag Tag, const FInstancedStruct& Payload)
{
    const TSet<uint32>* HandleIds = TagToHandles.Find(Tag);
    if (!HandleIds) return;

    // Copy IDs — callbacks may call Register/Unregister mid-dispatch.
    TArray<uint32> IdsCopy = HandleIds->Array();

    for (uint32 Id : IdsCopy)
    {
        FWatchEntry* Entry = Entries.Find(Id);
        if (!Entry || !Entry->Callback) continue;

        // Enforce scope: skip callback if net role does not match.
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
        if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
            Bus->StopListening(*BusHandle);
        BusHandles.Remove(Tag);
    }
}
```

## `Deinitialize`

```cpp
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
```

---

# Usage

## With scope (typical)

```cpp
WatchHandle = UGameCoreEventWatcher::Get(this)->Register(
    this,
    FGameplayTag::RequestGameplayTag("RequirementEvent.Leveling.LevelChanged"),
    EGameCoreEventScope::ServerOnly,
    [WeakThis](FGameplayTag Tag, const FInstancedStruct& Payload)
    {
        if (UMySystem* Self = WeakThis.Get())
            Self->OnLevelEvent(Payload);
    });
```

## Without scope (fires on all sides)

```cpp
WatchHandle = UGameCoreEventWatcher::Get(this)->Register(
    this,
    FGameplayTag::RequestGameplayTag("GameCoreEvent.UI.ScreenOpened"),
    [WeakThis](FGameplayTag, const FInstancedStruct& Payload)
    {
        if (UMySystem* Self = WeakThis.Get())
            Self->OnScreenOpened(Payload);
    });
```

## Multiple tags, one handle

```cpp
FGameplayTagContainer Tags;
Tags.AddTag(FGameplayTag::RequestGameplayTag("RequirementEvent.Inventory.ItemAdded"));
Tags.AddTag(FGameplayTag::RequestGameplayTag("RequirementEvent.Inventory.ItemRemoved"));

WatchHandle = UGameCoreEventWatcher::Get(this)->Register(
    this, Tags, EGameCoreEventScope::ServerOnly,
    [WeakThis](FGameplayTag Tag, const FInstancedStruct& Payload)
    {
        if (UMySystem* Self = WeakThis.Get())
            Self->OnInventoryEvent(Tag, Payload);
    });

// One Unregister removes all tag subscriptions.
UGameCoreEventWatcher::Get(this)->Unregister(WatchHandle);
```

---

# Known Limitations

- **No parent tag subscription.** Exact tag matching only. Register leaf tags explicitly.
- **No built-in coalescing.** Immediate dispatch per broadcast. Systems that need batching own that logic.
- **Callbacks fire synchronously.** Heavy work in a callback blocks the broadcast caller. Defer via timer or game thread task.
- **Re-entrancy copies IDs.** Safe, but a `Register` inside a callback takes effect from the next broadcast only.
