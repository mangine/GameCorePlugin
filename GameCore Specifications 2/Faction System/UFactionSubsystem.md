# UFactionSubsystem

**File:** `Factions/FactionSubsystem.h / .cpp`

`UFactionSubsystem` is a `UWorldSubsystem` responsible for loading the `UFactionRelationshipTable`, building the relationship cache at world start, and providing O(1) relationship queries to all callers. It is the single source of truth for faction relationships at runtime.

---

## Class Definition

```cpp
/**
 * UFactionSubsystem
 *
 * UWorldSubsystem. Loads UFactionRelationshipTable and builds RelationshipCache at world start.
 * Provides O(1) relationship queries used by UFactionComponent, AI, and interaction gating.
 *
 * Thread safety: NOT thread-safe. All methods must be called on the game thread.
 * RelationshipCache is written once during BuildCache() and read-only thereafter.
 */
UCLASS()
class GAMECORE_API UFactionSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:

    // ── UWorldSubsystem ───────────────────────────────────────────────────

    virtual void OnWorldBeginPlay(UWorld& World) override;

    // ── Relationship Queries (callable on server and client) ──────────────

    // Resolves the relationship between two specific faction tags.
    // Returns the cached explicit value, or resolves via DefaultRelationship
    // FMath::Min on a cache miss. Returns Neutral if either tag is unregistered.
    // Self-relationship (FactionA == FactionB) always returns Ally.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    EFactionRelationship GetRelationship(
        FGameplayTag FactionA, FGameplayTag FactionB) const;

    // Resolves the worst (least-friendly) relationship across all primary faction
    // pairs between two components. Checks LocalOverrides from both components
    // before the cache. Returns FallbackRelationship when either component has
    // no primary memberships.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    EFactionRelationship GetActorRelationship(
        const UFactionComponent* Source,
        const UFactionComponent* Target) const;

    // Returns all registered faction tags that are Hostile toward any primary
    // faction on Source. Excludes factions Source is already a member of.
    // Used by AI perception and combat targeting systems.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    void GetHostileFactions(
        const UFactionComponent* Source,
        TArray<FGameplayTag>& OutHostile) const;

    // Returns the UFactionDefinition for a given faction tag, or null if not registered.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    const UFactionDefinition* GetDefinition(FGameplayTag FactionTag) const;

    // Returns the faction tag whose ReputationProgression matches the given
    // progression tag. Returns invalid tag if not found.
    // Used by game module reputation wiring — see Architecture.md wiring guide.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    FGameplayTag FindFactionByReputationProgression(
        FGameplayTag ProgressionTag) const;

    // ── Dev Validation ────────────────────────────────────────────────────

#if !UE_BUILD_SHIPPING
    // Logs warnings/errors for:
    //   - Factions referenced in ExplicitRelationships not present in Factions array
    //   - Faction tags with no valid UFactionDefinition asset
    //   - Duplicate pair entries in ExplicitRelationships
    void ValidateTable() const;
#endif

private:

    // Populated once in OnWorldBeginPlay. Never mutated at runtime.
    TMap<FFactionSortedPair, EFactionRelationship> RelationshipCache;

    // Faction tag → loaded UFactionDefinition*.
    UPROPERTY()
    TMap<FGameplayTag, TObjectPtr<UFactionDefinition>> DefinitionMap;

    // Reputation progression tag → faction tag. Used by FindFactionByReputationProgression.
    TMap<FGameplayTag, FGameplayTag> ReputationProgressionMap;

    UPROPERTY()
    TObjectPtr<UFactionRelationshipTable> Table;

    void BuildCache();

    // Returns FMath::Min of the two factions' DefaultRelationship uint8 values.
    // Returns Neutral if either faction is not in DefinitionMap.
    EFactionRelationship ResolveDefault(
        FGameplayTag FactionA, FGameplayTag FactionB) const;

    // Checks LocalOverrides on Source and Target for any pair covering (FactionA, FactionB).
    // Returns true and sets OutRelationship if a local override is found.
    bool CheckLocalOverrides(
        const UFactionComponent* Source,
        const UFactionComponent* Target,
        FGameplayTag FactionA,
        FGameplayTag FactionB,
        EFactionRelationship& OutRelationship) const;
};
```

---

## `OnWorldBeginPlay`

```cpp
void UFactionSubsystem::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);

    const UGameCoreFactionSettings* Settings = UGameCoreFactionSettings::Get();
    if (!Settings || Settings->FactionRelationshipTable.IsNull())
    {
        UE_LOG(LogFaction, Fatal,
            TEXT("UFactionSubsystem: No FactionRelationshipTable assigned in Project Settings. "
                 "Assign one under Project Settings > GameCore > Factions."));
        return;
    }

    Table = Cast<UFactionRelationshipTable>(
        Settings->FactionRelationshipTable.TryLoad());
    if (!Table)
    {
        UE_LOG(LogFaction, Fatal,
            TEXT("UFactionSubsystem: FactionRelationshipTable asset failed to load."));
        return;
    }

    BuildCache();

#if !UE_BUILD_SHIPPING
    ValidateTable();
#endif
}
```

---

## `BuildCache`

```cpp
void UFactionSubsystem::BuildCache()
{
    RelationshipCache.Empty();
    DefinitionMap.Empty();
    ReputationProgressionMap.Empty();

    // 1. Load all UFactionDefinition assets.
    for (const TSoftObjectPtr<UFactionDefinition>& SoftDef : Table->Factions)
    {
        UFactionDefinition* Def = SoftDef.LoadSynchronous();
        if (!Def || !Def->FactionTag.IsValid()) continue;

        DefinitionMap.Add(Def->FactionTag, Def);

        if (!Def->ReputationProgression.IsNull())
        {
            if (ULevelProgressionDefinition* RepDef =
                Def->ReputationProgression.LoadSynchronous())
            {
                ReputationProgressionMap.Add(RepDef->ProgressionTag, Def->FactionTag);
            }
        }
    }

    // 2. Insert explicit pairs using sorted keys.
    for (const FFactionRelationshipOverride& Override : Table->ExplicitRelationships)
    {
        const FFactionSortedPair Key(Override.FactionA, Override.FactionB);
        RelationshipCache.Add(Key, Override.Relationship);
    }

    UE_LOG(LogFaction, Log,
        TEXT("UFactionSubsystem: Cache built — %d factions, %d explicit pairs."),
        DefinitionMap.Num(), RelationshipCache.Num());
}
```

> `LoadSynchronous` is used during `OnWorldBeginPlay`, before gameplay ticks. This is acceptable — data asset loads are fast and this is a one-time startup cost. The cache is immutable after this point.

---

## `GetRelationship`

```cpp
EFactionRelationship UFactionSubsystem::GetRelationship(
    FGameplayTag FactionA, FGameplayTag FactionB) const
{
    if (FactionA == FactionB)
        return EFactionRelationship::Ally;

    const FFactionSortedPair Key(FactionA, FactionB);
    if (const EFactionRelationship* Found = RelationshipCache.Find(Key))
        return *Found;

    return ResolveDefault(FactionA, FactionB);
}
```

---

## `GetActorRelationship`

```cpp
EFactionRelationship UFactionSubsystem::GetActorRelationship(
    const UFactionComponent* Source,
    const UFactionComponent* Target) const
{
    if (!Source || !Target)
        return EFactionRelationship::Neutral;

    TArray<FGameplayTag> SourceFactions, TargetFactions;
    for (const FFactionMembership& M : Source->Memberships.Items)
        if (M.bPrimary && M.FactionTag.IsValid()) SourceFactions.Add(M.FactionTag);
    for (const FFactionMembership& M : Target->Memberships.Items)
        if (M.bPrimary && M.FactionTag.IsValid()) TargetFactions.Add(M.FactionTag);

    // Either actor has no primary memberships — use component fallbacks.
    if (SourceFactions.IsEmpty() || TargetFactions.IsEmpty())
    {
        const uint8 A = (uint8)Source->FallbackRelationship;
        const uint8 B = (uint8)Target->FallbackRelationship;
        return (EFactionRelationship)FMath::Min(A, B);
    }

    EFactionRelationship Worst = EFactionRelationship::Ally;
    for (const FGameplayTag& SF : SourceFactions)
    {
        for (const FGameplayTag& TF : TargetFactions)
        {
            EFactionRelationship Candidate;
            if (!CheckLocalOverrides(Source, Target, SF, TF, Candidate))
                Candidate = GetRelationship(SF, TF);

            if ((uint8)Candidate < (uint8)Worst)
                Worst = Candidate;

            if (Worst == EFactionRelationship::Hostile)
                return EFactionRelationship::Hostile; // Short-circuit
        }
    }
    return Worst;
}
```

---

## `ResolveDefault`

```cpp
EFactionRelationship UFactionSubsystem::ResolveDefault(
    FGameplayTag FactionA, FGameplayTag FactionB) const
{
    const UFactionDefinition* DefA = DefinitionMap.FindRef(FactionA);
    const UFactionDefinition* DefB = DefinitionMap.FindRef(FactionB);

    if (!DefA || !DefB)
        return EFactionRelationship::Neutral;

    const uint8 A = (uint8)DefA->DefaultRelationship;
    const uint8 B = (uint8)DefB->DefaultRelationship;
    return (EFactionRelationship)FMath::Min(A, B);
}
```

---

## `CheckLocalOverrides`

```cpp
bool UFactionSubsystem::CheckLocalOverrides(
    const UFactionComponent* Source,
    const UFactionComponent* Target,
    FGameplayTag FactionA,
    FGameplayTag FactionB,
    EFactionRelationship& OutRelationship) const
{
    const FFactionSortedPair Key(FactionA, FactionB);

    auto CheckList = [&](const TArray<FFactionRelationshipOverride>& Overrides) -> bool
    {
        for (const FFactionRelationshipOverride& O : Overrides)
        {
            if (FFactionSortedPair(O.FactionA, O.FactionB) == Key)
            {
                OutRelationship = O.Relationship;
                return true;
            }
        }
        return false;
    };

    return CheckList(Source->LocalOverrides) || CheckList(Target->LocalOverrides);
}
```

---

## `GetHostileFactions`

```cpp
void UFactionSubsystem::GetHostileFactions(
    const UFactionComponent* Source,
    TArray<FGameplayTag>& OutHostile) const
{
    OutHostile.Reset();
    if (!Source) return;

    for (const auto& KV : DefinitionMap)
    {
        const FGameplayTag& CandidateTag = KV.Key;

        // Skip factions Source is already a member of.
        if (Source->IsMemberOf(CandidateTag)) continue;

        for (const FFactionMembership& SM : Source->Memberships.Items)
        {
            if (!SM.bPrimary) continue;
            if (GetRelationship(SM.FactionTag, CandidateTag) == EFactionRelationship::Hostile)
            {
                OutHostile.AddUnique(CandidateTag);
                break;
            }
        }
    }
}
```

---

## `GetDefinition` / `FindFactionByReputationProgression`

```cpp
const UFactionDefinition* UFactionSubsystem::GetDefinition(FGameplayTag FactionTag) const
{
    return DefinitionMap.FindRef(FactionTag);
}

FGameplayTag UFactionSubsystem::FindFactionByReputationProgression(
    FGameplayTag ProgressionTag) const
{
    if (const FGameplayTag* Found = ReputationProgressionMap.Find(ProgressionTag))
        return *Found;
    return FGameplayTag::EmptyTag;
}
```

---

## Thread Safety

`UFactionSubsystem` is **not thread-safe**. All methods must be called on the game thread. `RelationshipCache` and `DefinitionMap` are written once during `BuildCache` and read-only thereafter — no locks are needed for reads after startup.
