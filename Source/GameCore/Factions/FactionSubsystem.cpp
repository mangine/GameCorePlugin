// Copyright GameCore Plugin. All Rights Reserved.
#include "FactionSubsystem.h"
#include "FactionDeveloperSettings.h"
#include "FactionRelationshipTable.h"
#include "FactionDefinition.h"
#include "FactionComponent.h"

// ULevelProgressionDefinition is an optional dependency.
// The Progression system may not yet be implemented.
// We access ProgressionTag via the UObject reflection API to avoid a hard include.
// When the Progression system is present, include its header here directly.
// DEVIATION: ReputationProgressionMap population uses reflection fallback below.

DEFINE_LOG_CATEGORY(LogFaction);

namespace GameCoreEventTags
{
    FGameplayTag Faction_MemberJoined;
    FGameplayTag Faction_MemberLeft;
    FGameplayTag Faction_RankChanged;
}

// ─── Helper: register native tags once ───────────────────────────────────────

namespace
{
    struct FFactionTagRegistrar
    {
        FFactionTagRegistrar()
        {
            UE_CALL_ONCE([]
            {
                GameCoreEventTags::Faction_MemberJoined = FGameplayTag::RequestGameplayTag(
                    TEXT("GameCoreEvent.Faction.MemberJoined"));
                GameCoreEventTags::Faction_MemberLeft = FGameplayTag::RequestGameplayTag(
                    TEXT("GameCoreEvent.Faction.MemberLeft"));
                GameCoreEventTags::Faction_RankChanged = FGameplayTag::RequestGameplayTag(
                    TEXT("GameCoreEvent.Faction.RankChanged"));
            });
        }
    };
    static FFactionTagRegistrar GFactionTagRegistrar;
}

// =============================================================================
// UFactionSubsystem
// =============================================================================

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
            // Load the ULevelProgressionDefinition via soft pointer.
            // We use UObject reflection to read ProgressionTag without a hard header dependency.
            // When the Progression system header is available, replace this block with a
            // direct cast and member access.
            if (UObject* RepObj = Def->ReputationProgression.LoadSynchronous())
            {
                // Read the FGameplayTag property named "ProgressionTag" via reflection.
                if (const FProperty* Prop = RepObj->GetClass()->FindPropertyByName(
                    TEXT("ProgressionTag")))
                {
                    if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
                    {
                        const void* ValuePtr = StructProp->ContainerPtrToValuePtr<void>(RepObj);
                        FGameplayTag ProgressionTag;
                        StructProp->CopyCompleteValue(&ProgressionTag, ValuePtr);
                        if (ProgressionTag.IsValid())
                            ReputationProgressionMap.Add(ProgressionTag, Def->FactionTag);
                    }
                }
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

#if !UE_BUILD_SHIPPING
void UFactionSubsystem::ValidateTable() const
{
    if (!Table) return;

    // Collect all registered faction tags from DefinitionMap.
    TSet<FGameplayTag> RegisteredTags;
    for (const auto& KV : DefinitionMap)
        RegisteredTags.Add(KV.Key);

    // Check that all ExplicitRelationship factions are in the Factions array.
    for (const FFactionRelationshipOverride& Override : Table->ExplicitRelationships)
    {
        if (!RegisteredTags.Contains(Override.FactionA))
        {
            UE_LOG(LogFaction, Warning,
                TEXT("UFactionSubsystem::ValidateTable: FactionA [%s] in ExplicitRelationships "
                     "has no corresponding entry in Factions array."),
                *Override.FactionA.ToString());
        }
        if (!RegisteredTags.Contains(Override.FactionB))
        {
            UE_LOG(LogFaction, Warning,
                TEXT("UFactionSubsystem::ValidateTable: FactionB [%s] in ExplicitRelationships "
                     "has no corresponding entry in Factions array."),
                *Override.FactionB.ToString());
        }
    }
}
#endif
