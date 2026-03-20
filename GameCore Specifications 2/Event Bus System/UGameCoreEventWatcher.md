# UGameCoreEventWatcher

**File:** `GameCore/Source/GameCore/EventBus/GameCoreEventWatcher.h` / `.cpp`

`UGameCoreEventWatcher` is a `UWorldSubsystem` that bridges the event bus to registered callbacks. Any system registers a closure against one or more `FGameplayTag` channels under a single revocable handle. When a matching event arrives, the watcher enforces per-registration scope and fires the callback.

The watcher carries **zero domain knowledge**. It is a routing layer only.

---

## `FEventWatchHandle`

```cpp
// GameCoreEventWatcher.h

struct GAMECORE_API FEventWatchHandle
{
    uint32 Id = 0;

    bool IsValid()  const { return Id != 0; }
    bool operator==(const FEventWatchHandle& Other) const { return Id == Other.Id; }
    bool operator!=(const FEventWatchHandle& Other) const { return Id != Other.Id; }
};

inline uint32 GetTypeHash(const FEventWatchHandle& H) { return H.Id; }
```

- Handle `0` is always invalid.
- IDs are monotonically increasing per subsystem instance (resets on world teardown).
- A single handle covers all tags registered in one `Register` call.
- One `Unregister(Handle)` removes all associated tag subscriptions.

---

## Class Declaration

```cpp
// GameCoreEventWatcher.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "GameplayMessageSubsystem.h"
#include "GameCoreEventBus.h"
#include "GameCoreEventWatcher.generated.h"

UCLASS()
class GAMECORE_API UGameCoreEventWatcher : public UWorldSubsystem
{
    GENERATED_BODY()
public:

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    static UGameCoreEventWatcher* Get(const UObject* WorldContext);

    // ── Registration ──────────────────────────────────────────────────────────

    /**
     * Registers a callback for one or more tags.
     *
     * Scope controls which net role triggers delivery:
     *   ServerOnly  — fires only on server / standalone (not NM_Client)
     *   ClientOnly  — fires on NM_Client and NM_Standalone
     *   Both        — fires everywhere
     *
     * Use TWeakObjectPtr in the callback lambda for owner lifetime safety.
     * The watcher does not manage owner lifetime.
     *
     * @return FEventWatchHandle. Store it and pass to Unregister at teardown.
     */
    FEventWatchHandle Register(
        const UObject* Owner,
        const FGameplayTagContainer& Tags,
        EGameCoreEventScope Scope,
        TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback);

    /** Single-tag overload with explicit scope. */
    FEventWatchHandle Register(
        const UObject* Owner,
        FGameplayTag Tag,
        EGameCoreEventScope Scope,
        TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback);

    /** Single-tag overload with implicit Both scope. */
    FEventWatchHandle Register(
        const UObject* Owner,
        FGameplayTag Tag,
        TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback);

    /**
     * Removes the registration and all associated tag subscriptions.
     * Unsubscribes from bus if this was the last registration for a given tag.
     * Safe to call with an invalid handle.
     */
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

    TMap<uint32, FWatchEntry>                       Entries;        // Handle.Id → entry
    TMap<FGameplayTag, TSet<uint32>>                TagToHandles;   // Tag → set of Handle.Ids
    TMap<FGameplayTag, FGameplayMessageListenerHandle> BusHandles;  // Tag → GMS listener handle
    uint32 NextHandleId = 1;

    void SubscribeTagIfNeeded(FGameplayTag Tag);
    void UnsubscribeTagIfEmpty(FGameplayTag Tag);
    void OnBusEvent(FGameplayTag Tag, const FInstancedStruct& Payload);
    bool PassesScopeCheck(EGameCoreEventScope Scope) const;
};
```

---

## `.cpp` Implementation

### `Initialize` / `Deinitialize`

```cpp
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
```

### `Get`

```cpp
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
```

### `Register` (multi-tag, primary overload)

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
```

### `Register` (single-tag overloads)

```cpp
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

### `Unregister`

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

### `OnBusEvent` (re-entrant dispatch)

```cpp
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
```

> **Re-entrancy note:** `IdsCopy` is taken before dispatch. A `Register` inside a callback adds an entry but is not in `IdsCopy`, so it fires from the next broadcast only. An `Unregister` inside a callback removes from `Entries`, and the `Entries.Find(Id)` null-check handles the dangling ID safely.

### `PassesScopeCheck`

```cpp
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

### Lazy bus subscription helpers

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

---

## Design Notes

- **Raw callbacks only.** Payloads are `FInstancedStruct`. Domain logic (struct casting, field access) lives in the caller's closure.
- **Scope enforced at delivery**, not at registration. A `ServerOnly` registration never fires on a client even if the broadcast used `Both` scope.
- **Lazy bus subscription.** The watcher registers a GMS listener for a tag only when first needed, reducing idle overhead for unused channels.
- **`OwnerDebugName` stripped in shipping builds** — zero runtime cost in production.
