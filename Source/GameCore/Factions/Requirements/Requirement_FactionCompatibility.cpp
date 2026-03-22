// Copyright GameCore Plugin. All Rights Reserved.
#include "Requirement_FactionCompatibility.h"
#include "Factions/FactionComponent.h"
#include "Factions/FactionSubsystem.h"
#include "Factions/FactionDefinition.h"

#define LOCTEXT_NAMESPACE "FactionSystem"

FRequirementResult URequirement_FactionCompatibility::Evaluate(
    const FRequirementContext& Context) const
{
    // Unpack FFactionRequirementContext from Data.
    const FFactionRequirementContext* FactionCtx =
        Context.Data.GetPtr<FFactionRequirementContext>();

    if (!FactionCtx || !FactionCtx->Instigator) return FRequirementResult::Pass();

    const UFactionComponent* FC =
        FactionCtx->Instigator->FindComponentByClass<UFactionComponent>();
    if (!FC) return FRequirementResult::Pass(); // No factions = no conflict.

    const UFactionSubsystem* FS =
        FactionCtx->World
        ? FactionCtx->World->GetSubsystem<UFactionSubsystem>()
        : nullptr;
    if (!FS) return FRequirementResult::Pass();

    for (const FFactionMembership& M : FC->Memberships.Items)
    {
        if (!M.bPrimary) continue;

        const EFactionRelationship Rel =
            FS->GetRelationship(M.FactionTag, TargetFactionTag);

        if ((uint8)Rel < (uint8)MinimumAllowedRelationship)
        {
            const UFactionDefinition* Def = FS->GetDefinition(M.FactionTag);
            return FRequirementResult::Fail(
                FText::Format(
                    LOCTEXT("FactionConflict",
                        "Your membership in {0} conflicts with joining this faction."),
                    Def ? Def->DisplayName
                        : FText::FromName(M.FactionTag.GetTagName())));
        }
    }
    return FRequirementResult::Pass();
}

void URequirement_FactionCompatibility::GetWatchedEvents_Implementation(
    FGameplayTagContainer& OutEvents) const
{
    OutEvents.AddTag(
        FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Faction.MemberJoined")));
    OutEvents.AddTag(
        FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Faction.MemberLeft")));
}

#if WITH_EDITOR
FString URequirement_FactionCompatibility::GetDescription() const
{
    return FString::Printf(
        TEXT("FactionCompatibility: existing primaries >= %s toward %s"),
        *StaticEnum<EFactionRelationship>()->GetNameStringByValue(
            (int64)MinimumAllowedRelationship),
        *TargetFactionTag.ToString());
}
#endif

#undef LOCTEXT_NAMESPACE
