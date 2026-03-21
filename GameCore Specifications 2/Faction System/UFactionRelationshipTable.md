# UFactionRelationshipTable & UGameCoreFactionSettings

---

## `UFactionRelationshipTable`

**File:** `Factions/FactionRelationshipTable.h / .cpp`

Singleton project-wide `UPrimaryDataAsset` listing all factions and all explicit pair relationships. One instance per project, assigned in `UGameCoreFactionSettings`. `UFactionSubsystem` loads and validates this asset at world start.

```cpp
/**
 * UFactionRelationshipTable
 *
 * Singleton project asset. Lists all UFactionDefinition assets and all
 * explicit faction-pair relationships.
 *
 * One table per project, assigned in UGameCoreFactionSettings.
 * UFactionSubsystem uses this as the sole source for BuildCache().
 */
UCLASS(BlueprintType)
class GAMECORE_API UFactionRelationshipTable : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:

    // All UFactionDefinition assets registered for this project.
    // A faction referenced in ExplicitRelationships but absent here
    // is logged as an error in non-shipping builds by ValidateTable().
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Factions")
    TArray<TSoftObjectPtr<UFactionDefinition>> Factions;

    // Explicit faction-pair relationships.
    // Pairs are order-independent — UFactionSubsystem::BuildCache sorts before insertion.
    // If a pair is absent, UFactionSubsystem resolves via DefaultRelationship FMath::Min.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Relationships")
    TArray<FFactionRelationshipOverride> ExplicitRelationships;

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;
#endif
};
```

### `IsDataValid` — Editor Validation

```cpp
EDataValidationResult UFactionRelationshipTable::IsDataValid(
    FDataValidationContext& Context) const
{
    EDataValidationResult Result = Super::IsDataValid(Context);

    // Duplicate faction entries.
    TSet<FSoftObjectPath> SeenFactions;
    for (const TSoftObjectPtr<UFactionDefinition>& Def : Factions)
    {
        if (!SeenFactions.Add(Def.ToSoftObjectPath()).IsAlreadyInSet()) continue;
        Context.AddError(FText::Format(
            LOCTEXT("DuplicateFaction", "UFactionRelationshipTable: duplicate faction entry {0}."),
            FText::FromString(Def.GetAssetName())));
        Result = EDataValidationResult::Invalid;
    }

    // Duplicate explicit pairs.
    TSet<uint32> SeenPairs;
    for (const FFactionRelationshipOverride& Override : ExplicitRelationships)
    {
        const uint32 Hash = GetTypeHash(FFactionSortedPair(Override.FactionA, Override.FactionB));
        if (!SeenPairs.Add(Hash).IsAlreadyInSet()) continue;
        Context.AddError(FText::Format(
            LOCTEXT("DuplicatePair",
                "UFactionRelationshipTable: duplicate explicit pair {0} / {1}."),
            FText::FromName(Override.FactionA.GetTagName()),
            FText::FromName(Override.FactionB.GetTagName())));
        Result = EDataValidationResult::Invalid;
    }

    return Result;
}
```

---

## `UGameCoreFactionSettings`

**File:** `Factions/FactionDeveloperSettings.h / .cpp`

Exposes the relationship table asset to the editor and runtime. Accessible via **Project Settings → GameCore → Factions**.

```cpp
/**
 * UGameCoreFactionSettings
 *
 * Developer settings for the Faction System.
 * Accessible at Project Settings > GameCore > Factions.
 */
UCLASS(Config = Game, DefaultConfig,
    meta = (DisplayName = "Factions"))
class GAMECORE_API UGameCoreFactionSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UGameCoreFactionSettings()
    {
        CategoryName = TEXT("GameCore");
        SectionName  = TEXT("Factions");
    }

    // The single UFactionRelationshipTable asset for this project.
    // Must be set before entering PIE.
    // UFactionSubsystem logs UE_LOG Fatal if this is null at OnWorldBeginPlay.
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly,
        Category = "Factions",
        meta = (AllowedClasses = "FactionRelationshipTable"))
    FSoftObjectPath FactionRelationshipTable;

    static const UGameCoreFactionSettings* Get()
    {
        return GetDefault<UGameCoreFactionSettings>();
    }
};
```

---

## Notes

- Only **one** `UFactionRelationshipTable` is active per project. Multiple tables are not supported.
- Adding a faction to `Factions` but not to any `ExplicitRelationships` entry is valid — it simply uses `DefaultRelationship` resolution for all its pairs.
- `ExplicitRelationships` entries whose tags are missing from `Factions` are detected by `UFactionSubsystem::ValidateTable` (non-shipping only).
