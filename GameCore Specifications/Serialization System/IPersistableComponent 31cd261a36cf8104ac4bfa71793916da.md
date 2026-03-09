# IPersistableComponent

# IPersistableComponent

**Module:** `GameCore`

**Location:** `GameCore/Source/GameCore/Persistence/PersistableComponent.h`

**Type:** UInterface

The core interface that any actor component implements to participate in the persistence system. Handles serialization, deserialization, schema versioning, and migration. All orchestration is owned by `UPersistenceSubsystem` and `UPersistenceRegistrationComponent`.

---

## Responsibilities

- Serialize component state to/from a binary `FArchive`
- Declare a schema version for safe evolution
- Implement migration logic when a saved version is older than the current version
- Notify the owning registration component when state changes (dirty marking)

---

## Interface Declaration

```cpp
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UPersistableComponent : public UInterface { GENERATED_BODY() };

class GAMECORE_API IPersistableComponent
{
    GENERATED_BODY()

public:
    // Unique key identifying this component's blob in the payload.
    // Must be stable across versions — never rename after shipping.
    virtual FName GetPersistenceKey() const = 0;

    // Current schema version. Increment when the serialized layout changes.
    virtual uint32 GetSchemaVersion() const = 0;

    // Write current state into Ar.
    // Must be read-only — no state mutation during serialization.
    virtual void Serialize_Save(FArchive& Ar) = 0;

    // Read state from Ar.
    // SavedVersion is the version stored in the blob being loaded.
    // Subsystem calls Migrate() first if SavedVersion != GetSchemaVersion().
    virtual void Serialize_Load(FArchive& Ar, uint32 SavedVersion) = 0;

    // Called automatically before Serialize_Load when versions mismatch.
    // Ar is positioned at the start of the old blob.
    // Default no-op is safe for purely additive schema changes.
    virtual void Migrate(FArchive& Ar, uint32 FromVersion, uint32 ToVersion) {}

    // Dirty tracking
    uint32 DirtyGeneration = 0;
    bool bDirty = false;

protected:
    // Lazy-cached — resolved once on first dirty notification.
    mutable TWeakObjectPtr<UPersistenceRegistrationComponent> CachedRegComp;

    // Call this inside the implementing component on any meaningful state change.
    void NotifyDirty(UActorComponent* Self);
};
```

---

## NotifyDirty Implementation

```cpp
void IPersistableComponent::NotifyDirty(UActorComponent* Self)
{
    if (bDirty) return; // Guard: only notify once until cleared

    if (!CachedRegComp.IsValid())
    {
        CachedRegComp = Self->GetOwner()
            ->FindComponentByClass<UPersistenceRegistrationComponent>();
    }

    if (CachedRegComp.IsValid())
    {
        bDirty = true;
        CachedRegComp->MarkDirty();
    }
}
```

> `FindComponentByClass` is called **once per component lifetime** thanks to the lazy cache and `bDirty` guard. All subsequent dirty calls hit only a bool check and a weak pointer validity test — effectively free.
> 

---

## Dirty Clearing — Generation Counter

Prevents incorrectly clearing dirty state when a component becomes dirty **during** an in-progress flush.

```cpp
// Called by UPersistenceRegistrationComponent::BuildPayload after serializing
void ClearIfSaved(uint32 FlushedGeneration)
{
    if (DirtyGeneration <= FlushedGeneration)
        bDirty = false;
    // DirtyGeneration > FlushedGeneration means a newer dirty happened
    // during the flush — keep dirty, picked up next cycle.
}
```

`DirtyGeneration` is set to the registration component's `SaveGeneration` at the moment `NotifyDirty` is called.

---

## Migration Flow

```
[Serialize_Load called — SavedVersion = 2, GetSchemaVersion() = 3]
    │
    ▼
[Subsystem detects version mismatch]
    │
    ▼
[Calls Migrate(Ar, FromVersion=2, ToVersion=3)]
    │  Component reads old layout, transforms data in-place
    ▼
[Calls Serialize_Load with transformed state]
```

Additive-only changes (new fields with defaults) do not require overriding `Migrate()` — the default no-op is safe. Structural or destructive changes must override.

---

## Save & Load — Implementation Example

```cpp
class UInventoryComponent : public UActorComponent, public IPersistableComponent
{
    GENERATED_BODY()
public:
    virtual FName GetPersistenceKey() const override { return TEXT("Inventory"); }
    virtual uint32 GetSchemaVersion() const override { return 2; }

    virtual void Serialize_Save(FArchive& Ar) override
    {
        int32 Count = Items.Num();
        Ar << Count;
        for (auto& Item : Items)
            Ar << Item;
    }

    virtual void Serialize_Load(FArchive& Ar, uint32 SavedVersion) override
    {
        int32 Count;
        Ar << Count;
        Items.SetNum(Count);
        for (auto& Item : Items)
            Ar << Item;
    }

    virtual void Migrate(FArchive& Ar, uint32 From, uint32 To) override
    {
        if (From == 1 && To == 2)
        {
            // Version 1 had no Durability field — inject defaults
            for (auto& Item : Items)
                Item.Durability = 100;
        }
    }

    void AddItem(FInventoryItem Item)
    {
        Items.Add(Item);
        NotifyDirty(this); // Single call — all wiring handled
    }

private:
    TArray<FInventoryItem> Items;
};
```

---

## Dirty Trigger Types

| Change Type | Example | Should Dirty? | Notes |
| --- | --- | --- | --- |
| `CriticalState` | Item gained, level up, quest complete | Yes | Always enqueue immediately |
| `WorldState` | Position, rotation | No (explicit) | Saved by periodic sweep regardless |
| `TransientState` | Animation state, visual FX | No | Never persisted |

> **Future:** A `EPersistenceDirtyReason` parameter on `NotifyDirty` could allow the subsystem to fast-track critical changes. Not required now.
> 

---

## Notes & Caveats

- `GetPersistenceKey()` must **never be renamed** after shipping — it is the blob identifier in saved payloads.
- `Serialize_Save` must be **strictly read-only** — no state mutation during serialization.
- Implementing components must call `NotifyDirty(this)` themselves — there is no automatic change detection.
- The interface has no replication concern — that is separate.
- If `CachedRegComp` is invalid at `NotifyDirty` time (actor has no registration component), the dirty call is silently dropped. Log a warning during development builds.

---

## Future Improvements

- `EPersistenceDirtyReason` parameter for priority routing
- Dev-build validation that `GetPersistenceKey()` is unique across components on the same actor