#include "Quest/Requirements/Requirement_QuestCooldown.h"
#include "Quest/Components/QuestComponent.h"
#include "Quest/Subsystems/QuestRegistrySubsystem.h"
#include "GameFramework/PlayerState.h"
#include "Engine/GameInstance.h"

#define LOCTEXT_NAMESPACE "QuestRequirements"

FRequirementResult URequirement_QuestCooldown::Evaluate(
    const FRequirementContext& Context) const
{
    const FQuestEvaluationContext* CtxData =
        Context.Data.GetPtr<FQuestEvaluationContext>();
    if (!CtxData || !CtxData->PlayerState)
        return FRequirementResult::Pass(); // no context → pass silently

    const UQuestComponent* QC =
        CtxData->PlayerState->FindComponentByClass<UQuestComponent>();
    if (!QC) return FRequirementResult::Pass();

    const FQuestRuntime* Runtime = QC->FindActiveQuest(QuestIdKey);
    const int64 LastCompleted = Runtime ? Runtime->LastCompletedTimestamp : 0;
    if (LastCompleted <= 0) return FRequirementResult::Pass();

    const int64 NowTs = FDateTime::UtcNow().ToUnixTimestamp();

    if (Cadence == EQuestResetCadence::None)
    {
        const int64 Required = static_cast<int64>(CooldownSeconds);
        const int64 Elapsed  = NowTs - LastCompleted;
        if (Elapsed >= Required) return FRequirementResult::Pass();
        return FRequirementResult::Fail(
            FText::Format(
                LOCTEXT("Cooldown", "Available in {0}s"),
                FText::AsNumber(Required - Elapsed)));
    }

    // Cadence-based: check against last reset timestamp.
    const UQuestRegistrySubsystem* Registry =
        CtxData->World
            ? CtxData->World->GetGameInstance()
                  ->GetSubsystem<UQuestRegistrySubsystem>()
            : nullptr;
    if (!Registry) return FRequirementResult::Pass();

    int64 LastReset = 0;
    if (Cadence == EQuestResetCadence::Daily)
        LastReset = Registry->GetLastDailyResetTimestamp();
    else if (Cadence == EQuestResetCadence::Weekly)
        LastReset = Registry->GetLastWeeklyResetTimestamp();

    if (LastCompleted < LastReset) return FRequirementResult::Pass();

    return FRequirementResult::Fail(
        LOCTEXT("NotReset", "Quest has not yet reset."));
}

#undef LOCTEXT_NAMESPACE
