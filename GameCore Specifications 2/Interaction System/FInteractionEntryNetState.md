# `FInteractionEntryNetState` and `FInteractionEntryNetStateArray`

**File:** `Interaction/Data/InteractionNetState.h`

The **only data replicated per entry**. Static config is never sent over the wire. One `FInteractionEntryNetState` item exists per entry, delta-serialized via `FFastArraySerializer` — only changed items are sent per replication cycle.

---

```cpp
#pragma once
#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Interaction/Enums/InteractionEnums.h"
#include "InteractionNetState.generated.h"

class UInteractionComponent;

USTRUCT()
struct FInteractionEntryNetState : public FFastArraySerializerItem
{
    GENERATED_BODY()

    // Stable flat index. Set at BeginPlay, never changes.
    // Used by PostReplicatedChange to identify which entry changed.
    UPROPERTY()
    uint8 EntryIndex = 0;

    // Gameplay state managed by game systems via SetEntryState().
    // Available / Occupied / Cooldown / Disabled are valid server-set values.
    // Locked is NEVER stored here — it is a client-only evaluation result.
    UPROPERTY()
    EInteractableState State = EInteractableState::Available;

    // Administrative enable flag. Managed independently from State.
    // When false, entry is invisible to clients regardless of State.
    // Use for live-ops control, bug mitigation, or server-driven feature flags.
    // Kept separate from State so game systems can manage gameplay state freely
    // without accidentally re-enabling an administratively disabled entry.
    UPROPERTY()
    bool bServerEnabled = true;

    // ── FFastArraySerializer Callbacks ────────────────────────────────────────
    // Fire on clients after replication delivers an update.
    // All three delegate to OwningComponent to broadcast OnEntryStateChanged.

    void PostReplicatedAdd(const struct FInteractionEntryNetStateArray& Array);
    void PostReplicatedChange(const struct FInteractionEntryNetStateArray& Array);
    void PreReplicatedRemove(const struct FInteractionEntryNetStateArray& Array);
};

USTRUCT()
struct FInteractionEntryNetStateArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FInteractionEntryNetState> Items;

    // Bridge from FFastArraySerializer callbacks (which fire on structs) to delegates
    // (which are on UObjects). Set in UInteractionComponent::BeginPlay on all machines.
    UPROPERTY(NotReplicated)
    TObjectPtr<UInteractionComponent> OwningComponent;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<
            FInteractionEntryNetState,
            FInteractionEntryNetStateArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FInteractionEntryNetStateArray>
    : TStructOpsTypeTraitsBase2<FInteractionEntryNetStateArray>
{
    enum { WithNetDeltaSerializer = true };
};
```

### Notes

- **`bServerEnabled` is separate from `State` by design.** `State` reflects gameplay conditions managed by game systems. `bServerEnabled` is an administrative override for operators. Keeping them separate means a game system can freely set `State = Occupied` without accidentally re-enabling an entry that was administratively disabled.
- **`Locked` is never in `FInteractionEntryNetState`.** It is a client-only result of tag/requirement evaluation. Never pass `Locked` to `SetEntryState` — it will be silently rejected.
- **`OwningComponent` is the GC-safe bridge.** `FFastArraySerializer` callbacks fire on structs, not UObjects. `OwningComponent` provides access to the broadcast delegate. Must be set in `BeginPlay` before any replication arrives.
