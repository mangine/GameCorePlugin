# URequirementWatcherManager

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

`URequirementWatcherManager` is the event bus bridge for the Requirement System. It subscribes to `RequirementEvent.*` tags on the GameCore event bus, receives `FInstancedStruct` payloads, and routes them to all `URequirementList` instances that subscribed to those tags. It does not evaluate requirements — it only routes.

**File:** `Requirements/RequirementWatcher.h / .cpp`

---

# Class Definition

```cpp
UCLASS()
class GAMECORE_API URequirementWatcherManager : public UWorldSubsystem
{
    GENERATED_BODY()
public:

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ── List Registration (called by URequirementList) ──────────────────────

    // Subscribes a list to a set of event tags.
    // Called by URequirementList::Register().
    // The list is stored as a weak pointer — no ownership transfer.
    void RegisterList(URequirementList* List, const FGameplayTagContainer& WatchedTags);

    // Removes all subscriptions for this list.
    // Called by URequirementList::Unregister().
    void UnregisterList(URequirementList* List);

    // ── Event Broadcasting (called by game systems) ─────────────────────────

    // Broadcasts an event to all lists watching EventTag.
    // Payload is the FInstancedStruct from the event bus — passed through
    // directly to URequirementList::NotifyEvent.
    //
    // Authority is enforced here: lists whose Authority does not match
    // the current net role are silently skipped.
    //
    // Coalescing: multiple BroadcastEvent calls with the same tag in the
    // same frame are batched. Each list receives at most one NotifyEvent
    // call per frame per tag, using the last payload received.
    UFUNCTION(BlueprintCallable, Category = "Requirements")
    void BroadcastEvent(FGameplayTag EventTag, const FInstancedStruct& Payload);

private:
    // Tag → registered lists.
    TMap<FGameplayTag, TArray<TWeakObjectPtr<URequirementList>>> TagToLists;

    // Coalescing: pending events accumulated this frame.
    TMap<FGameplayTag, FInstancedStruct> PendingEvents;

    // Frame flush handle.
    FDelegateHandle PostWorldTickHandle;

    // Event bus listener handle — stored to unsubscribe on Deinitialize.
    FDelegateHandle EventBusHandle;

    void OnEventBusMessage(FGameplayTag Tag, const FInstancedStruct& Payload);
    void FlushPendingEvents();
    bool IsAuthorityMatch(const URequirementList* List) const;
};
```

---

# Initialization — Event Bus Subscription

```cpp
void URequirementWatcherManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Subscribe to all RequirementEvent.* tags on the event bus.
    // UGameCoreEventBus is the GameCore event subsystem.
    UGameCoreEventBus* Bus = GetWorld()->GetSubsystem<UGameCoreEventBus>();
    if (Bus)
    {
        EventBusHandle = Bus->Subscribe(
            FGameplayTag::RequestGameplayTag("RequirementEvent"),
            FGameCoreEventDelegate::CreateUObject(
                this, &URequirementWatcherManager::OnEventBusMessage));
    }

    // Register per-frame flush.
    PostWorldTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(
        this, &URequirementWatcherManager::FlushPendingEvents_Wrapper);
}

void URequirementWatcherManager::Deinitialize()
{
    if (UGameCoreEventBus* Bus = GetWorld()->GetSubsystem<UGameCoreEventBus>())
        Bus->Unsubscribe(EventBusHandle);

    FWorldDelegates::OnWorldPostActorTick.Remove(PostWorldTickHandle);
    Super::Deinitialize();
}
```

---

# Event Flow

```
Game system changes state (e.g. player levels up)
  │
  └── Fires event on UGameCoreEventBus:
        Tag     = "RequirementEvent.Leveling.LevelChanged"
        Payload = FInstancedStruct containing FLevelChangedEvent

URequirementWatcherManager::OnEventBusMessage(Tag, Payload)
  │
  └── Stores in PendingEvents[Tag] = Payload  (last-write-wins per tag per frame)

[end of frame — OnWorldPostActorTick]
URequirementWatcherManager::FlushPendingEvents()
  │
  └── For each (Tag, Payload) in PendingEvents:
        Lists = TagToLists[Tag]
        For each TWeakObjectPtr<URequirementList> WeakList in Lists:
          if (!WeakList.IsValid()) → remove stale entry, continue
          if (!IsAuthorityMatch(WeakList.Get())) → skip (wrong net side)
          WeakList->NotifyEvent(Tag, Payload)
            │
            └── URequirementList evaluates all requirements via EvaluateFromEvent
                If pass/fail state changed → OnResultChanged.Broadcast(bPassed)
                Consuming system callback fires
```

---

# `RegisterList` / `UnregisterList`

```cpp
void URequirementWatcherManager::RegisterList(
    URequirementList* List, const FGameplayTagContainer& WatchedTags)
{
    for (const FGameplayTag& Tag : WatchedTags)
    {
        TArray<TWeakObjectPtr<URequirementList>>& Entry = TagToLists.FindOrAdd(Tag);
        // Avoid duplicate registration.
        Entry.AddUnique(TWeakObjectPtr<URequirementList>(List));
    }
}

void URequirementWatcherManager::UnregisterList(URequirementList* List)
{
    for (auto& Pair : TagToLists)
    {
        Pair.Value.RemoveAll([List](const TWeakObjectPtr<URequirementList>& W)
        {
            return !W.IsValid() || W.Get() == List;
        });
    }
}
```

---

# Authority Enforcement

```cpp
bool URequirementWatcherManager::IsAuthorityMatch(const URequirementList* List) const
{
    if (!List) return false;

    ENetMode NetMode = GetWorld()->GetNetMode();
    switch (List->Authority)
    {
    case ERequirementEvalAuthority::ServerOnly:
        return NetMode != NM_Client;
    case ERequirementEvalAuthority::ClientOnly:
        return NetMode == NM_Client || NetMode == NM_Standalone;
    case ERequirementEvalAuthority::ClientValidated:
        return true; // both sides evaluate; server re-validates via RPC
    }
    return false;
}
```

---

# Coalescing Rationale

Without coalescing, a burst of 20 `ItemAdded` events in one frame would call `NotifyEvent` 20 times on the same list. Most of those evaluations are redundant — only the final world state matters. Coalescing batches events per tag per frame, guaranteeing each list evaluates at most once per tag per frame regardless of event rate. Last-write-wins for the payload is correct because the requirement evaluates current state, not event history.

---

# Event Tag Conventions

All requirement invalidation tags live under `RequirementEvent.*`. Each module owns its sub-namespace.

```
RequirementEvent
  ├── Leveling.LevelChanged
  ├── Inventory.ItemAdded
  ├── Inventory.ItemRemoved
  ├── Quest.StageChanged
  ├── Quest.Completed
  ├── Combat.EnemyKilled
  ├── Tag.TagAdded
  └── Tag.TagRemoved
```

**Rules:**
- Tags defined in `DefaultGameplayTags.ini` in their owning module — never in a central file.
- Use specific leaf tags, not parent tags. `Inventory.ItemAdded` not `Inventory`.
- Cache tag handles at module startup via `UGameplayTagsManager::AddNativeGameplayTag`.

---

# Firing an Event — Integration Example

Any system that changes state requirements may watch fires the event on the bus:

```cpp
// In ULevelingComponent, after a level-up:
void ULevelingComponent::OnLevelUp(APlayerState* PS, int32 OldLevel, int32 NewLevel)
{
    // ... apply level-up effects ...

    FLevelChangedEvent Payload;
    Payload.PlayerState = PS;
    Payload.OldLevel    = OldLevel;
    Payload.NewLevel    = NewLevel;

    UGameCoreEventBus* Bus = GetWorld()->GetSubsystem<UGameCoreEventBus>();
    if (Bus)
    {
        Bus->Broadcast(
            FGameplayTag::RequestGameplayTag("RequirementEvent.Leveling.LevelChanged"),
            FInstancedStruct::Make(Payload));
    }
}
```

The watcher manager receives this automatically. No direct call to the manager is needed from the leveling system.

---

# Known Limitations

- **Stale weak pointers.** `TagToLists` entries are cleaned up lazily during `FlushPendingEvents`. Lists that are garbage-collected between registration and the next flush are silently skipped and removed.
- **Last-write-wins coalescing.** If two different payloads for the same tag arrive in the same frame (e.g. two enemies killed), only the last payload is delivered to lists. Requirements that need to process every individual event should not rely on the watcher — they should be evaluated directly by the owning system.
- **No per-consumer routing.** All registered lists for a tag receive the same event payload. Requirements that need to filter by player identity must do so inside their `EvaluateFromEvent` implementation.
