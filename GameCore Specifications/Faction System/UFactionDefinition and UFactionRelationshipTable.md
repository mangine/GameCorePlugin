# UFactionDefinition & UFactionRelationshipTable

**Sub-page of:** [Faction System](../Faction%20System.md)

Data assets that define the static configuration of factions and their relationships. Loaded once at world startup. Never modified at runtime.

---

## `UFactionDefinition`

**File:** `Factions/FactionDefinition.h / .cpp`

Designer-authored data asset, one per faction. Contains all static configuration for a faction: its identity, default relationship stance, join requirements, reputation link, and rank progression.

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UFactionDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:

    // ── Identity ────────────────────────────────────────────────────────

    // Unique gameplay tag for this faction.
    // Convention: Faction.<Group>.<Name> — e.g. Faction.Navy, Faction.Pirates.BlackSails
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FGameplayTag FactionTag;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FText DisplayName;

    // Optional flavour icon for UI. Loaded on demand.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    TSoftObjectPtr<UTexture2D> Icon;

    // ── Relationships ────────────────────────────────────────────────────

    // Fallback relationship used when no explicit pair entry exists in the table
    // for a query involving this faction.
    // Resolution: FMath::Min(DefaultA, DefaultB) — least friendly always wins.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Relationship")
    EFactionRelationship DefaultRelationship = EFactionRelationship::Neutral;

    // ── Join Requirements ────────────────────────────────────────────────

    // Evaluated server-side when JoinFaction() is called.
    // Must all be synchronous — validated at BeginPlay in non-shipping builds.
    // Example: URequirement_MinLevel, URequirement_FactionCompatibility,
    //          URequirement_QuestCompleted.
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Join")
    TArray<TObjectPtr<URequirement>> JoinRequirements;

    // ── Reputation (optional) ────────────────────────────────────────────

    // Soft reference to the Progression definition that tracks reputation XP
    // for this faction (e.g. DA_Progression_NavyReputation).
    // GameCore does NOT listen to progression events or map levels to ranks.
    // This field is documentation/config for game module wiring only.
    // See the Reputation/Rank Wiring Guide on the main Faction System page.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reputation",
        meta = (ToolTip = "GameCore does not wire this automatically. See Faction System wiring guide."))
    TSoftObjectPtr<ULevelProgressionDefinition> ReputationProgression;

    // Max level of the reputation progression, used by game module wiring
    // to map progression level → rank index. Only meaningful when
    // ReputationProgression is set.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reputation",
        meta = (EditCondition = "ReputationProgression != nullptr", ClampMin = 1))
    int32 MaxReputationLevel = 100;

    // ── Ranks ────────────────────────────────────────────────────────────

    // Ordered list of rank tags, lowest to highest.
    // e.g. Faction.Navy.Rank.Sailor, Faction.Navy.Rank.Officer, Faction.Navy.Rank.Admiral
    // Empty = no rank system for this faction.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranks")
    TArray<FGameplayTag> RankTags;

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;
#endif
};
```

### Editor Validation

`IsDataValid` checks:
- `FactionTag` is valid and non-empty.
- `FactionTag` matches the asset name convention (warning, not error).
- `RankTags` contains no duplicates.
- `MaxReputationLevel > 0` when `ReputationProgression` is set.
- All `JoinRequirements` are synchronous (calls `URequirementLibrary::ValidateRequirements`).

---

## `UFactionRelationshipTable`

**File:** `Factions/FactionRelationshipTable.h / .cpp`

Singleton project-wide asset listing all factions and all explicit pair relationships. One instance per project, assigned in `UGameCoreFactionSettings`. `UFactionSubsystem` loads and validates this asset at world start.

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UFactionRelationshipTable : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:

    // All UFactionDefinition assets registered for this project.
    // UFactionSubsystem loads and validates each one at world start.
    // A faction referenced in ExplicitRelationships but absent here
    // is logged as an error in non-shipping builds.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Factions")
    TArray<TSoftObjectPtr<UFactionDefinition>> Factions;

    // Explicit faction-pair relationships. Pairs are order-independent.
    // If a pair is absent, UFactionSubsystem resolves via DefaultRelationship min().
    // Entries are sorted by UFactionSubsystem::BuildCache before insertion —
    // authoring order does not matter.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Relationships")
    TArray<FFactionRelationshipOverride> ExplicitRelationships;

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;
#endif
};
```

### Editor Validation

`IsDataValid` checks:
- No duplicate faction entries in `Factions`.
- No duplicate pairs in `ExplicitRelationships`.
- Both tags in every `ExplicitRelationships` entry are present in `Factions`.

---

## `UGameCoreFactionSettings`

**File:** `Factions/FactionDeveloperSettings.h / .cpp`

Exposes the relationship table asset to the editor and runtime. Accessible via **Project Settings → GameCore → Factions**.

```cpp
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
    // Must be set before entering PIE. UFactionSubsystem logs a fatal error
    // if this is null at OnWorldBeginPlay.
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
