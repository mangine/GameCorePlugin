// Tags/Requirements/RequirementHasTag.h
#pragma once

#include "CoreMinimal.h"
#include "Requirements/Requirement.h"
#include "GameplayTagContainer.h"
#include "Tags/TaggedInterface.h"
#include "RequirementHasTag.generated.h"

UCLASS(DisplayName = "Has Gameplay Tag")
class GAMECORE_API URequirement_HasTag : public URequirement
{
    GENERATED_BODY()

public:
    // The tag the subject must own for this requirement to pass.
    UPROPERTY(EditAnywhere, Category = "Requirement")
    FGameplayTag RequiredTag;

    // If true, uses HasTagExact — the subject must own this exact tag with no parent match.
    // If false (default), uses HasTag — parent tags satisfy the check (standard UE behaviour).
    UPROPERTY(EditAnywhere, Category = "Requirement")
    bool bExactMatch = false;

    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const override
    {
        if (!RequiredTag.IsValid())
            return FRequirementResult::Fail(LOCTEXT("HasTag_InvalidTag", "Requirement has no tag configured."));

        // Resolve subject: prefer Instigator, fall back to PlayerState's pawn.
        AActor* Subject = Context.Instigator
            ? Context.Instigator
            : (Context.PlayerState ? Context.PlayerState->GetPawn() : nullptr);

        if (!Subject)
            return FRequirementResult::Fail(LOCTEXT("HasTag_NoSubject", "No valid subject actor for tag check."));

        ITaggedInterface* Tagged = Cast<ITaggedInterface>(Subject);
        if (!Tagged)
            return FRequirementResult::Fail(LOCTEXT("HasTag_NoInterface", "Subject does not implement ITaggedInterface."));

        const FGameplayTagContainer& Tags = Tagged->GetGameplayTags();
        const bool bPasses = bExactMatch ? Tags.HasTagExact(RequiredTag) : Tags.HasTag(RequiredTag);

        return bPasses
            ? FRequirementResult::Pass()
            : FRequirementResult::Fail(FText::Format(
                LOCTEXT("HasTag_Missing", "Missing required tag: {0}"),
                FText::FromName(RequiredTag.GetTagName())));
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("Has Tag: %s%s"),
            *RequiredTag.ToString(),
            bExactMatch ? TEXT(" (exact)") : TEXT(""));
    }
#endif
};
