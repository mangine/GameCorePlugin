// Copyright GameCore Plugin. All Rights Reserved.
#include "FactionDefinition.h"
#include "Requirements/Requirement.h"

#define LOCTEXT_NAMESPACE "FactionSystem"

#if WITH_EDITOR

EDataValidationResult UFactionDefinition::IsDataValid(FDataValidationContext& Context) const
{
    EDataValidationResult Result = Super::IsDataValid(Context);

    if (!FactionTag.IsValid())
    {
        Context.AddError(LOCTEXT("MissingFactionTag",
            "UFactionDefinition: FactionTag must be set."));
        Result = EDataValidationResult::Invalid;
    }

    // Duplicate rank tags.
    TSet<FGameplayTag> Seen;
    for (const FGameplayTag& Rank : RankTags)
    {
        if (!Seen.Add(Rank).IsAlreadyInSet()) continue;
        Context.AddError(FText::Format(
            LOCTEXT("DuplicateRank", "UFactionDefinition: duplicate RankTag {0}."),
            FText::FromName(Rank.GetTagName())));
        Result = EDataValidationResult::Invalid;
    }

    if (MaxReputationLevel <= 0 && !ReputationProgression.IsNull())
    {
        Context.AddError(LOCTEXT("BadMaxRepLevel",
            "UFactionDefinition: MaxReputationLevel must be > 0 when ReputationProgression is set."));
        Result = EDataValidationResult::Invalid;
    }

    // Validate that all JoinRequirements are synchronous.
    // (Heuristic: purely event-driven requirements expose watched events.
    //  Synchronous requirements expose none. See spec notes.)
    for (const TObjectPtr<URequirement>& Req : JoinRequirements)
    {
        if (!Req) continue;
        // Heuristic check reserved for future enforcement.
        // For now, presence of watched events is informational only.
    }

    return Result;
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
