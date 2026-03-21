# IPersistableComponent

**Module:** `GameCore`  
**Location:** `GameCore/Source/GameCore/Persistence/PersistableComponent.h/.cpp`  
**Type:** `UInterface`

The interface any actor component implements to participate in the persistence system. Handles serialization, deserialization, schema versioning, and dirty notification. All orchestration is owned by `UPersistenceSubsystem` and `UPersistenceRegistrationComponent`.

---

## Important: Dirty State Lives on the Implementing Class

`IPersistableComponent` is a `UInterface` — it cannot own UObject-managed instance data. The dirty-tracking fields (`bDirty`, `DirtyGeneration`, `CachedRegComp`) and the `NotifyDirty` helper **must be declared on the implementing component class** (the `UActorComponent` subclass), not inherited from the interface.

The interface declares the contract; the implementing class provides the storage.

---

## Interface Declaration

```cpp
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UPersistableComponent : public UInterface { GENERATED_BODY() };

class GAMECORE_API IPersistableComponent
{
    GENERATED_BODY()

public:
    /**
     * Unique key identifying this component's blob in the payload.
     * Must be stable across versions — never rename after shipping.
     */
    virtual FName GetPersistenceKey() const = 0;

    /** Current schema version. Increment when the serialized layout changes. */
    virtual uint32 GetSchemaVersion() const = 0;

    /**
     * Write current state into Ar.
     * Must be strictly read-only — no state mutation during serialization.
     */
    virtual void Serialize_Save(FArchive& Ar) = 0;

    /**
     * Read state from Ar.
     * SavedVersion is the version stored in the blob being loaded.
     * UPersistenceSubsystem calls Migrate() before this if versions mismatch.
     */
    virtual void Serialize_Load(FArchive& Ar, uint32 SavedVersion) = 0;

    /**
     * Called automatically before Serialize_Load when SavedVersion != GetSchemaVersion().
     * Ar is positioned at the start of the old blob.
     * Default no-op is safe for purely additive schema changes.
     */
    virtual void Migrate(FArchive& Ar, uint32 FromVersion, uint32 ToVersion) {}

    /**
     * Clear dirty state if no newer dirty has occurred since this flush.
     * Called by UPersistenceRegistrationComponent::BuildPayload after serializing this component.
     * Implementing class must provide bDirty and DirtyGeneration as instance members.
     */
    virtual void ClearIfSaved(uint32 FlushedGeneration) = 0;
};
```

> **Note:** `ClearIfSaved` is declared pure virtual here so the interface forces the implementing class to provide it along with the instance state it needs (`bDirty`, `DirtyGeneration`). The implementation is boilerplate — see the example below.

---

## Required State on Implementing Class

Every class implementing `IPersistableComponent` **must** declare these fields and helpers:

```cpp
// Inside the UActorComponent subclass — NOT on the interface
bool   bDirty          = false;
uint32 DirtyGeneration = 0;
mutable TWeakObjectPtr<UPersistenceRegistrationComponent> CachedRegComp;

/** Lazy resolution + dirty propagation. Call on any meaningful state change. */
void NotifyDirty(UActorComponent* Self)
{
    if (bDirty) return;

    if (!CachedRegComp.IsValid())
        CachedRegComp = Self->GetOwner()
            ->FindComponentByClass<UPersistenceRegistrationComponent>();

    if (CachedRegComp.IsValid())
    {
        DirtyGeneration = CachedRegComp->SaveGeneration;
        bDirty = true;
        CachedRegComp->MarkDirty();
    }
    // If no RegComp found: silently skip. Log in dev builds.
}

/** IPersistableComponent implementation */
virtual void ClearIfSaved(uint32 FlushedGeneration) override
{
    if (DirtyGeneration <= FlushedGeneration)
        bDirty = false;
    // DirtyGeneration > FlushedGeneration: a newer dirty occurred during flush — stay dirty.
}
```

> `FindComponentByClass` is called **once per component lifetime** due to the lazy cache and `bDirty` guard. All subsequent calls are a bool check + weak pointer validity test — effectively free.

---

## Generation Counter Contract

Prevents incorrectly clearing dirty state when a component is dirtied **during** an in-progress flush:

1. Component dirtied → `DirtyGeneration = CachedRegComp->SaveGeneration` (current generation)
2. `BuildPayload` runs → `++SaveGeneration` on the registration component
3. `ClearIfSaved(SaveGeneration)` called → `DirtyGeneration <= FlushedGeneration` → clear
4. If component dirtied **again** between steps 2 and 3 → `DirtyGeneration > FlushedGeneration` → stays dirty → picked up next cycle

---

## Migration Flow

```
[Serialize_Load called — SavedVersion=2, GetSchemaVersion()=3]
    │
    ▼
[UPersistenceSubsystem detects version mismatch]
    │
    ▼
[Calls Migrate(Ar, FromVersion=2, ToVersion=3)]
    │  Component reads old layout, transforms data in-place
    ▼
[Calls Serialize_Load with SavedVersion=2]
    Component reads using current layout conventions
```

Additive-only changes (new fields with defaults) do not require overriding `Migrate()` — the default no-op is safe. Structural or destructive changes must override.

---

## Full Implementation Example

```cpp
// InventoryComponent.h
UCLASS()
class UInventoryComponent : public UActorComponent, public IPersistableComponent
{
    GENERATED_BODY()
public:
    // IPersistableComponent
    virtual FName   GetPersistenceKey() const override { return TEXT("Inventory"); }
    virtual uint32  GetSchemaVersion()  const override { return 2; }
    virtual void    Serialize_Save(FArchive& Ar) override;
    virtual void    Serialize_Load(FArchive& Ar, uint32 SavedVersion) override;
    virtual void    Migrate(FArchive& Ar, uint32 From, uint32 To) override;
    virtual void    ClearIfSaved(uint32 FlushedGeneration) override;

    // Required dirty-tracking state (storage owned by this class)
    bool   bDirty          = false;
    uint32 DirtyGeneration = 0;
    mutable TWeakObjectPtr<UPersistenceRegistrationComponent> CachedRegComp;

    void NotifyDirty(UActorComponent* Self); // implemented in .cpp

    // Game logic
    void AddItem(FInventoryItem Item);

private:
    TArray<FInventoryItem> Items;
};

// InventoryComponent.cpp
void UInventoryComponent::Serialize_Save(FArchive& Ar)
{
    int32 Count = Items.Num();
    Ar << Count;
    for (auto& Item : Items) Ar << Item;
}

void UInventoryComponent::Serialize_Load(FArchive& Ar, uint32 SavedVersion)
{
    int32 Count;
    Ar << Count;
    Items.SetNum(Count);
    for (auto& Item : Items) Ar << Item;
}

void UInventoryComponent::Migrate(FArchive& Ar, uint32 From, uint32 To)
{
    if (From == 1 && To == 2)
    {
        // V1 had no Durability — inject defaults
        for (auto& Item : Items)
            Item.Durability = 100;
    }
}

void UInventoryComponent::ClearIfSaved(uint32 FlushedGeneration)
{
    if (DirtyGeneration <= FlushedGeneration)
        bDirty = false;
}

void UInventoryComponent::NotifyDirty(UActorComponent* Self)
{
    if (bDirty) return;
    if (!CachedRegComp.IsValid())
        CachedRegComp = Self->GetOwner()
            ->FindComponentByClass<UPersistenceRegistrationComponent>();
    if (CachedRegComp.IsValid())
    {
        DirtyGeneration = CachedRegComp->SaveGeneration;
        bDirty = true;
        CachedRegComp->MarkDirty();
    }
}

void UInventoryComponent::AddItem(FInventoryItem Item)
{
    Items.Add(Item);
    NotifyDirty(this);
}
```

---

## Dirty Trigger Guidelines

| Change Type | Should NotifyDirty? | Notes |
|---|---|---|
| Item gained / lost | Yes | Critical state |
| Level up, stat change | Yes | Critical state |
| Position / rotation | No | Saved by periodic full cycle |
| Animation, VFX state | No | Transient — never persisted |

---

## Notes & Constraints

- `GetPersistenceKey()` must **never be renamed** after shipping — it is the blob identifier.
- `Serialize_Save` must be **strictly read-only** — no state mutation during serialization.
- If `CachedRegComp` is invalid at `NotifyDirty` time (actor has no `UPersistenceRegistrationComponent`), the call is silently dropped. Add a `UE_LOG` warning in non-shipping builds.
- The interface has no replication concern — persistence is server-side only.
- Dev build: validate `GetPersistenceKey()` uniqueness across components on the same actor at `BeginPlay`.
