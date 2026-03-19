# FSpawnEntry

**Module:** `GameCore`
**File:** `GameCore/Source/GameCore/Spawning/SpawnEntry.h`

Per-entity-type configuration struct. One entry describes a single entity class that the `USpawnManagerComponent` should keep alive in the world, along with its count limits, spawn rate cap, eligibility requirements, and optional loot override. Multiple entries on the same component allow different entity types to coexist on one anchor.

---

## Struct Declaration

```cpp
// File: GameCore/Source/GameCore/Spawning/SpawnEntry.h

USTRUCT(BlueprintType)
struct GAMECORE_API FSpawnEntry
{
    GENERATED_BODY()

    // ── Entity Class ──────────────────────────────────────────────────────────

    /**
     * The actor class to spawn. Must implement ISpawnableEntity.
     * Soft reference — loaded asynchronously on first flow tick.
     * Filtered in the editor to ISpawnableEntity implementors via FSpawnEntryCustomization.
     */
    UPROPERTY(EditAnywhere, Category = "Spawn Entry",
        meta = (GameCoreInterfaceFilter = "SpawnableEntity"))
    TSoftClassPtr<AActor> EntityClass;

    // ── Count Limits ─────────────────────────────────────────────────────────

    /**
     * Maximum number of live instances of this entry at any time.
     * Spawn attempts for this entry stop once LiveInstances reaches MaxCount.
     */
    UPROPERTY(EditAnywhere, Category = "Spawn Entry", meta = (ClampMin = 1))
    int32 MaxCount = 1;

    /**
     * Maximum spawns of this entry per flow tick.
     * 0 = no per-entry cap; only the global FlowCount budget applies.
     * Use to prevent one entry from consuming the entire global budget on a tick.
     */
    UPROPERTY(EditAnywhere, Category = "Spawn Entry", meta = (ClampMin = 0))
    int32 MaxPerTick = 0;

    // ── Requirements ─────────────────────────────────────────────────────────

    /**
     * Optional eligibility gate. Evaluated once per flow tick against a
     * world-state-only FRequirementContext (no PlayerState).
     * If the list fails, this entry is skipped for this tick entirely —
     * no individual instance attempts are made.
     *
     * Typical uses: time-of-day restriction, weather state, quest phase.
     * Null = always eligible.
     */
    UPROPERTY(EditAnywhere, Category = "Spawn Entry")
    TObjectPtr<URequirementList> SpawnRequirements;

    // ── Loot Table Override ──────────────────────────────────────────────────

    /**
     * Optional loot table override for this entry.
     * Not loaded or used by the spawn manager itself.
     * Passed to the entity via ISpawnableEntity::OnSpawnedByManager so the
     * entity can apply it to its own loot component.
     * Null = entity uses its own default loot table.
     */
    UPROPERTY(EditAnywhere, Category = "Spawn Entry")
    TSoftObjectPtr<ULootTable> LootTableOverride;

    // ── Runtime State (not UPROPERTY — ephemeral, never persisted) ────────────

    /**
     * Weak pointers to currently tracked live instances.
     * Populated by USpawnManagerComponent when an actor is spawned.
     * Entries are removed eagerly via OnDestroyed binding.
     * Invalid entries are pruned at the start of each flow tick as a safety net.
     */
    TArray<TWeakObjectPtr<AActor>> LiveInstances;

    // ── Runtime Helpers ───────────────────────────────────────────────────────

    /**
     * Prunes invalid weak pointers and returns the current live count.
     * Called at the start of each flow tick before vacancy calculation.
     */
    int32 GetAndPruneLiveCount()
    {
        LiveInstances.RemoveAll(
            [](const TWeakObjectPtr<AActor>& P){ return !P.IsValid(); });
        return LiveInstances.Num();
    }

    /** Returns how many more instances can be spawned right now. */
    int32 GetVacancy() const
    {
        return FMath::Max(0, MaxCount - LiveInstances.Num());
    }

    /**
     * Returns the effective per-tick spawn budget for this entry,
     * given the remaining global budget.
     * MaxPerTick = 0 means uncapped (bounded only by global budget).
     */
    int32 GetEffectiveBudget(int32 GlobalBudgetRemaining) const
    {
        if (MaxPerTick <= 0)
            return GlobalBudgetRemaining;
        return FMath::Min(MaxPerTick, GlobalBudgetRemaining);
    }
};
```

---

## Requirement Evaluation Context

When `SpawnRequirements` is set, `USpawnManagerComponent` builds a minimal `FRequirementContext` before calling `Evaluate`:

```cpp
FRequirementContext Ctx;
Ctx.World      = GetWorld();
Ctx.Instigator = GetOwner(); // Anchor actor
// PlayerState is intentionally null.
// SpawnRequirements must not reference player-state requirements.
```

Requirements that expect a non-null `PlayerState` will trivially fail on a spawn entry — this is a misconfiguration and will be caught by `URequirementLibrary::ValidateRequirements` in development builds.

In development builds, `USpawnManagerComponent::BeginPlay` calls `ValidateRequirements` on every `FSpawnEntry::SpawnRequirements` list to catch player-state-dependent requirements that would never pass.

---

## LiveInstances Lifecycle

```
SpawnActor succeeds
  └─► LiveInstances.Add(WeakPtr)
        Actor.OnDestroyed bound → OnSpawnedActorDestroyed(Actor)

OnSpawnedActorDestroyed(Actor):
  └─► LiveInstances.RemoveAll(entry matches actor)
        // Slot is immediately vacant.

On each flow tick (safety net):
  └─► GetAndPruneLiveCount()
        // Removes any stale weak pointers not caught by OnDestroyed
        // (e.g. actors destroyed via subsystem flush without broadcast).
```

---

## Editor Customization: FSpawnEntryCustomization

**File:** `GameCoreEditor/Spawning/FSpawnEntryCustomization.h / .cpp`

Registered in `FGameCoreEditorModule::StartupModule` for `FSpawnEntry`. Replaces the default `EntityClass` picker with a class picker filtered to `ISpawnableEntity` implementors.

```cpp
// Registration:
PropertyModule.RegisterCustomPropertyTypeLayout(
    FSpawnEntry::StaticStruct()->GetFName(),
    FOnGetPropertyTypeCustomizationInstance::CreateStatic(
        &FSpawnEntryCustomization::MakeInstance));

// Unregistration:
PropertyModule.UnregisterCustomPropertyTypeLayout(
    FSpawnEntry::StaticStruct()->GetFName());
```

Implementation pattern — `CustomizeChildren`:

```cpp
void FSpawnEntryCustomization::CustomizeChildren(
    TSharedRef<IPropertyHandle>       StructHandle,
    IDetailChildrenBuilder&           Builder,
    IPropertyTypeCustomizationUtils&  Utils)
{
    // Iterate children. For EntityClass: replace with filtered class picker.
    // For all other properties: add with default rendering.
    uint32 NumChildren;
    StructHandle->GetNumChildren(NumChildren);
    for (uint32 i = 0; i < NumChildren; ++i)
    {
        TSharedRef<IPropertyHandle> Child = StructHandle->GetChildHandle(i).ToSharedRef();
        const FName PropName = Child->GetProperty()
            ? Child->GetProperty()->GetFName()
            : NAME_None;

        if (PropName == GET_MEMBER_NAME_CHECKED(FSpawnEntry, EntityClass))
        {
            Builder.AddCustomRow(LOCTEXT("EntityClass", "Entity Class"))
                .NameContent()[ Child->CreatePropertyNameWidget() ]
                .ValueContent()
                [
                    SNew(SObjectPropertyEntryBox)
                        .PropertyHandle(Child)
                        .AllowedClass(AActor::StaticClass())
                        .OnShouldFilterAsset_Static(
                            &GameCoreEditorUtils::AssetImplementsInterface,
                            USpawnableEntity::StaticClass())
                        .ThumbnailPool(Utils.GetThumbnailPool())
                ];
        }
        else
        {
            Builder.AddProperty(Child);
        }
    }
}
```

---

## Notes

- `LiveInstances` is declared outside any `#if` guard and is not a `UPROPERTY`. It is purely transient runtime state, reset implicitly when the component begins play. It must never be serialized — if it were ever accidentally made a `UPROPERTY`, it would serialize stale pointers to actors that no longer exist.
- `SpawnRequirements` is a `TObjectPtr` (hard reference to asset), not `TSoftObjectPtr`. Requirement list assets are small and must be loaded before the first flow tick. Soft-loading them would add async complexity for negligible benefit.
- `LootTableOverride` is `TSoftObjectPtr` because loot tables can be large and are only needed by the entity itself, not the spawner.
- Entry order in `USpawnManagerComponent::SpawnEntries` acts as implicit spawn priority when the global budget is exhausted mid-array. Document this for designers.
