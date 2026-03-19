# UFactionSubsystem

**Sub-page of:** [Faction System](../Faction%20System.md)

The `UFactionSubsystem` is a `UWorldSubsystem` responsible for loading the `UFactionRelationshipTable`, building the relationship cache at world start, and providing O(1) relationship queries to all callers. It is the single source of truth for faction relationships at runtime.

**File:** `Factions/FactionSubsystem.h / .cpp`

---

## Class Definition

```cpp
UCLASS()
class GAMECORE_API UFactionSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:

    // ── UWorldSubsystem ──────────────────────────────────────────────────

    virtual void OnWorldBeginPlay(UWorld& World) override;

    // ── Relationship Queries (callable on server and client) ─────────────

    // Resolves the relationship between two specific faction tags.
    // Returns the cached explicit value, or resolves via DefaultRelationship
    // min() on a cache miss.
    // If either tag has no UFactionDefinition, returns Neutral.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    EFactionRelationship GetRelationship(
        FGameplayTag FactionA, FGameplayTag FactionB) const;

    // Resolves the worst (least-friendly) relationship across all primary faction
    // pairs between two components. Applies LocalOverrides from both components
    // before the cache. Returns FallbackRelationship when either component has
    // no primary memberships.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    EFactionRelationship GetActorRelationship(
        const UFactionComponent* Source,
        const UFactionComponent* Target) const;

    // Returns all faction tags (primary memberships only) that are Hostile to
    // any primary faction on Source. Used by AI perception and combat targeting.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    void GetHostileFactions(
        const UFactionComponent* Source,
        TArray<FGameplayTag>& OutHostile) const;

    // Returns the definition for a given faction tag, or null if not registered.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    const UFactionDefinition* GetDefinition(FGameplayTag FactionTag) const;

    // Returns the faction tag whose ReputationProgression matches the given
    // progression tag. Returns invalid tag if not found.
    // Used by game module reputation wiring — see Faction System wiring guide.
    UFUNCTION(BlueprintCallable, Category = "Factions")
    FGameplayTag FindFactionByReputationProgression(
        FGameplayTag ProgressionTag) const;

    // ── Dev Validation ───────────────────────────────────────────────────

#if !UE_BUILD_SHIPPING
    // Logs errors for:
    //   - Factions referenced in ExplicitRelationships not present in Factions array
    //   - Faction tags listed in Factions with no valid UFactionDefinition asset
    //   - Duplicate pair entries in ExplicitRelationships
    void ValidateTable() const;
#endif

private:

    // Populated once in OnWorldBeginPlay. Never mutated at runtime.
    // Key: FFactionSortedPair. Value: explicit EFactionRelationship.
    // Cache miss = no explicit pair exists; resolve via ResolveDefault().
    TMap<FFactionSortedPair, EFactionRelationship> RelationshipCache;

    // Faction tag → loaded UFactionDefinition. Populated in BuildCache.
    UPROPERTY()
    TMap<FGameplayTag, TObjectPtr<UFactionDefinition>> DefinitionMap;

    // Reputation progression tag → faction tag. Populated in BuildCache.
    // Used by FindFactionByReputationProgression.
    TMap<FGameplayTag, FGameplayTag> ReputationProgressionMap;

    UPROPERTY()
    TObjectPtr<UFactionRelationshipTable> Table;

    void BuildCache();

    // Returns FMath::Min of the two factions' DefaultRelationship uint8 values.
    // Returns Neutral if either faction has no definition.
    EFactionRelationship ResolveDefault(
        FGameplayTag FactionA, FGameplayTag FactionB) const;

    // Checks Source and Target LocalOverrides for any pair covering (FactionA, FactionB).
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

    // 1. Load all UFactionDefinition assets and populate DefinitionMap.
    for (const TSoftObjectPtr<UFactionDefinition>& SoftDef : Table->Factions)
    {
        UFactionDefinition* Def = SoftDef.LoadSynchronous();
        if (!Def || !Def->FactionTag.IsValid()) continue;

        DefinitionMap.Add(Def->FactionTag, Def);

        // Populate reputation progression reverse map.
        if (!Def->ReputationProgression.IsNull())
        {
            // Load soft pointer to read ProgressionTag.
            if (ULevelProgressionDefinition* RepDef = Def->ReputationProgression.LoadSynchronous())
                ReputationProgressionMap.Add(RepDef->ProgressionTag, Def->FactionTag);
        }
    }

    // 2. Insert all explicit pairs into RelationshipCache using sorted keys.
    for (const FFactionRelationshipOverride& Override : Table->ExplicitRelationships)
    {
        FFactionSortedPair Key(Override.FactionA, Override.FactionB);
        RelationshipCache.Add(Key, Override.Relationship);
    }

    UE_LOG(LogFaction, Log,
        TEXT("UFactionSubsystem: Cache built. %d factions, %d explicit pairs."),
        DefinitionMap.Num(), RelationshipCache.Num());
}
```

**Note:** `LoadSynchronous` is used during `OnWorldBeginPlay` which occurs before gameplay. This is acceptable — data asset loads are fast and this is a one-time startup cost.

---

## `GetRelationship`

```cpp
EFactionRelationship UFactionSubsystem::GetRelationship(
    FGameplayTag FactionA, FGameplayTag FactionB) const
{
    // Self-relationship is always Ally.
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

    // Collect primary faction tags from each component.
    TArray<FGameplayTag> SourceFactions, TargetFactions;
    for (const FFactionMembership& M : Source->Memberships.Items)
        if (M.bPrimary && M.FactionTag.IsValid()) SourceFactions.Add(M.FactionTag);
    for (const FFactionMembership& M : Target->Memberships.Items)
        if (M.bPrimary && M.FactionTag.IsValid()) TargetFactions.Add(M.FactionTag);

    // If either actor has no primary factions, use component fallback.
    if (SourceFactions.IsEmpty() || TargetFactions.IsEmpty())
        return FMath::Min(
            (uint8)Source->FallbackRelationship,
            (uint8)Target->FallbackRelationship) > (uint8)EFactionRelationship::Hostile
            ? (EFactionRelationship)FMath::Min(
                (uint8)Source->FallbackRelationship,
                (uint8)Target->FallbackRelationship)
            : EFactionRelationship::Neutral;

    // Resolve worst relationship across all primary faction pairs.
    EFactionRelationship Worst = EFactionRelationship::Ally;
    for (const FGameplayTag& SF : SourceFactions)
    {
        for (const FGameplayTag& TF : TargetFactions)
        {
            EFactionRelationship Candidate;
            // Check local overrides first.
            if (!CheckLocalOverrides(Source, Target, SF, TF, Candidate))
                Candidate = GetRelationship(SF, TF);

            if ((uint8)Candidate < (uint8)Worst)
                Worst = Candidate;

            // Short-circuit: can't get worse than Hostile.
            if (Worst == EFactionRelationship::Hostile)
                return EFactionRelationship::Hostile;
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

        // Check if Source has this faction in its own memberships (skip self).
        if (Source->IsMemberOf(CandidateTag)) continue;

        // Build a temporary single-faction component view for the candidate.
        // We check the relationship from Source toward the candidate.
        for (const FFactionMembership& SM : Source->Memberships.Items)
        {
            if (!SM.bPrimary) continue;
            EFactionRelationship Rel = GetRelationship(SM.FactionTag, CandidateTag);
            if (Rel == EFactionRelationship::Hostile)
            {
                OutHostile.AddUnique(CandidateTag);
                break;
            }
        }
    }
}
```

---

## Thread Safety

`UFactionSubsystem` is **not thread-safe**. All methods must be called on the game thread. `RelationshipCache` and `DefinitionMap` are written once during `BuildCache` (game thread, world start) and read-only thereafter. No locks are needed for reads after startup, but do not call `BuildCache` from async tasks.
