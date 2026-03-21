# FSpawnEntry

**Module:** `GameCore`  
**File:** `GameCore/Source/GameCore/Spawning/SpawnEntry.h`

Per-entity-type configuration struct. One entry describes a single entity class the `USpawnManagerComponent` should keep alive in the world, along with its count limits, per-tick spawn cap, eligibility requirements, and optional loot override. Multiple entries on the same component allow different entity types to coexist at one anchor.

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
     * Soft reference — loaded asynchronously at BeginPlay.
     * Filtered in the editor to ISpawnableEntity implementors via FSpawnEntryCustomization.
     */
    UPROPERTY(EditAnywhere, Category = "Spawn Entry")
    TSoftClassPtr<AActor> EntityClass;

    // ── Count Limits ─────────────────────────────────────────────────────────

    /**
     * Maximum number of live instances of this entry at any time.
     * Spawn attempts stop once LiveInstances.Num() reaches MaxCount.
     */
    UPROPERTY(EditAnywhere, Category = "Spawn Entry", meta = (ClampMin = 1))
    int32 MaxCount = 1;

    /**
     * Maximum spawns of this entry per flow tick.
     * 0 = no per-entry cap; only the global FlowCount budget applies.
     */
    UPROPERTY(EditAnywhere, Category = "Spawn Entry", meta = (ClampMin = 0))
    int32 MaxPerTick = 0;

    // ── Requirements ─────────────────────────────────────────────────────────

    /**
     * Optional eligibility gate. Evaluated once per flow tick against a
     * world-state-only FRequirementContext (PlayerState = null).
     * If the list fails, this entry is skipped for the entire tick.
     *
     * Typical uses: time-of-day restriction, weather state, quest phase.
     * Null = always eligible.
     *
     * Hard reference (TObjectPtr) — requirement lists are small and must be
     * loaded before the first flow tick. Soft-loading adds async complexity
     * for negligible memory benefit.
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
     *
     * Soft reference — loot tables can be large and are only needed by the
     * entity itself, not by the spawner.
     */
    UPROPERTY(EditAnywhere, Category = "Spawn Entry")
    TSoftObjectPtr<ULootTable> LootTableOverride;

    // ── Runtime State (not UPROPERTY — ephemeral, never persisted) ────────────

    /**
     * Weak pointers to currently live instances.
     * Populated by USpawnManagerComponent after a successful spawn.
     * Removed eagerly via OnDestroyed binding; pruned on each flow tick as
     * a safety net for actors destroyed via paths that bypass OnDestroyed.
     *
     * Must never be a UPROPERTY — serialising stale actor pointers causes
     * hard-to-diagnose issues on level reload.
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
     * clamped to the remaining global budget.
     * MaxPerTick = 0 means no per-entry cap (bounded only by global budget).
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
// PlayerState is intentionally null — spawn requirements are world-state only.
```

Requirements that expect a non-null `PlayerState` will trivially fail on a spawn entry. In non-Shipping builds, `USpawnManagerComponent::BeginPlay` calls `URequirementLibrary::ValidateRequirements` on every entry's `SpawnRequirements` to catch this misconfiguration early.

---

## LiveInstances Lifecycle

```
TrySpawnForEntry succeeds
  └─► Entry.LiveInstances.Add(TWeakObjectPtr(Actor))
      Actor.OnDestroyed bound → USpawnManagerComponent::OnSpawnedActorDestroyed

OnSpawnedActorDestroyed(Actor):
  └─► LiveInstances.RemoveAll(ptr == Actor || !ptr.IsValid())
      // Slot is immediately vacant for the next flow tick.

Each flow tick (safety net):
  └─► GetAndPruneLiveCount()
      // Removes stale weak pointers for actors destroyed via world flush
      // or other paths that bypass the OnDestroyed broadcast.
```

---

## Editor Customization: FSpawnEntryCustomization

**File:** `GameCoreEditor/Source/GameCoreEditor/Spawning/FSpawnEntryCustomization.h / .cpp`

Registered in `FGameCoreEditorModule::StartupModule` for `FSpawnEntry`. Replaces the default `EntityClass` picker with a class picker filtered to `ISpawnableEntity` implementors.

```cpp
// Registration (StartupModule):
PropertyModule.RegisterCustomPropertyTypeLayout(
    FSpawnEntry::StaticStruct()->GetFName(),
    FOnGetPropertyTypeCustomizationInstance::CreateStatic(
        &FSpawnEntryCustomization::MakeInstance));

// Unregistration (ShutdownModule):
PropertyModule.UnregisterCustomPropertyTypeLayout(
    FSpawnEntry::StaticStruct()->GetFName());
```

`CustomizeChildren` implementation pattern:

```cpp
void FSpawnEntryCustomization::CustomizeChildren(
    TSharedRef<IPropertyHandle>      StructHandle,
    IDetailChildrenBuilder&          Builder,
    IPropertyTypeCustomizationUtils& Utils)
{
    uint32 NumChildren;
    StructHandle->GetNumChildren(NumChildren);
    for (uint32 i = 0; i < NumChildren; ++i)
    {
        TSharedRef<IPropertyHandle> Child = StructHandle->GetChildHandle(i).ToSharedRef();
        const FName PropName = Child->GetProperty()
            ? Child->GetProperty()->GetFName() : NAME_None;

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

- `LiveInstances` is not a `UPROPERTY`. Making it one would serialize stale actor pointers that no longer exist after level reload — a hard-to-diagnose crash vector.
- Entry order in `USpawnManagerComponent::SpawnEntries` acts as implicit spawn priority when the global budget is exhausted mid-array. Put highest-priority entity types first.
- `SpawnRequirements` is a `TObjectPtr` (hard reference) because requirement list assets are small and must be available before the first flow tick fires.
- `LootTableOverride` is `TSoftObjectPtr` because loot tables can be large and are only needed by the entity itself at post-spawn time.
