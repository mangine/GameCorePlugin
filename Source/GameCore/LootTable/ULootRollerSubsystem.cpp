// Copyright GameCore Plugin. All Rights Reserved.
#include "LootTable/ULootRollerSubsystem.h"
#include "LootTable/ULootTable.h"
#include "Requirements/Requirement.h"
#include "Requirements/RequirementContext.h"
#include "Core/Backend/GameCoreBackend.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"

// ============================================================================
// Gameplay tags — registered in DefaultGameplayTags.ini
// ============================================================================

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GameCore_Audit_Loot_Roll, "GameCore.Audit.Loot.Roll");

DEFINE_LOG_CATEGORY_STATIC(LogLootTable, Log, All);

// ============================================================================
// Primary Entry Point
// ============================================================================

TArray<FLootReward> ULootRollerSubsystem::RunLootTable(
    const ULootTable*       Table,
    const FLootRollContext& Context)
{
    // Server-only guard.
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (World && World->GetNetMode() == NM_Client)
    {
        UE_LOG(LogLootTable, Warning,
            TEXT("ULootRollerSubsystem::RunLootTable called on client — returning empty."));
        return {};
    }

    if (!ensureMsgf(Table != nullptr,
        TEXT("ULootRollerSubsystem::RunLootTable: Table is null.")))
    {
        return {};
    }

    // Seed setup — done once before any rolls.
    // If Context.Seed != INDEX_NONE, derive FinalSeed to prevent reverse-engineering.
    TOptional<FRandomStream> StreamStorage;
    FRandomStream* Stream = nullptr;
    int32 FinalSeed = INDEX_NONE;

    if (Context.Seed != INDEX_NONE)
    {
        const int32 RandomOffset = FMath::RandRange(0, MAX_int32);
        FinalSeed = static_cast<int32>(HashCombine(
            static_cast<uint32>(Context.Seed),
            static_cast<uint32>(RandomOffset)));
        StreamStorage.Emplace(FinalSeed);
        Stream = &StreamStorage.GetValue();
    }

    TArray<FLootReward> Results = RollTableInternal(Table, Context, 0, Stream);

    // Audit — always fires, even when Results is empty.
    FGameCoreBackend::GetAudit(TAG_GameCore_Audit_Loot_Roll).RecordEvent(
        Context.Instigator.Get(),
        Context.SourceTag,
        Table,
        FinalSeed,
        Context.LuckBonus,
        Results);

    return Results;
}

// ============================================================================
// Luck Modifier API
// ============================================================================

float ULootRollerSubsystem::ResolveLuckBonus(
    APlayerState* Instigator,
    FGameplayTag  SourceTag) const
{
    float Total = 0.0f;

    // Sum all registered modifiers where SourceTag matches (hierarchy match).
    for (const auto& Pair : Modifiers)
    {
        const FLootModifier& Mod = Pair.Value;
        if (Mod.ContextTag.IsValid() && SourceTag.MatchesTag(Mod.ContextTag))
        {
            Total += Mod.Bonus;
        }
    }

    // Add GAS Attribute.Luck from the instigator's pawn ASC if present.
    // The concrete FGameplayAttribute for Luck is game-module-defined and cannot
    // be referenced here without coupling to the game module. Games that want
    // GAS-driven luck should subclass ULootRollerSubsystem and override
    // ResolveLuckBonus, calling Super and adding the ASC lookup.
    // See DEVIATIONS.md for details.
    if (Instigator)
    {
        if (APawn* Pawn = Instigator->GetPawn())
        {
            if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn))
            {
                if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
                {
                    // Luck attribute lookup is intentionally skipped here.
                    // The game module must provide the FGameplayAttribute reference.
                    // Total += ASC->GetNumericAttribute(YourAttributeSet::GetLuckAttribute());
                    (void)ASC; // Suppress unused warning.
                }
            }
        }
    }

    // Clamp to >= 0. Never apply negative luck.
    return FMath::Max(0.0f, Total);
}

FLootModifierHandle ULootRollerSubsystem::RegisterModifier(
    FGameplayTag ContextTag, float Bonus)
{
    FLootModifierHandle Handle;
    Handle.Id = NextHandleId++;
    Modifiers.Add(Handle, FLootModifier{ ContextTag, FMath::Max(0.f, Bonus) });
    return Handle;
}

void ULootRollerSubsystem::UnregisterModifier(FLootModifierHandle Handle)
{
    if (Handle.IsValid())
        Modifiers.Remove(Handle);
}

// ============================================================================
// Private — Roll Algorithm
// ============================================================================

TArray<FLootReward> ULootRollerSubsystem::RollTableInternal(
    const ULootTable*       Table,
    const FLootRollContext& Context,
    int32                   CurrentDepth,
    FRandomStream*          Stream)
{
    if (!ensureMsgf(CurrentDepth <= MaxRecursionDepth,
        TEXT("ULootRollerSubsystem: MaxRecursionDepth (%d) exceeded. "
             "Possible circular nested table reference."),
        MaxRecursionDepth))
    {
        return {};
    }

    if (!Table) return {};

    // Resolve roll count from the table's RollCount range.
    const int32 RollCountMin = Table->RollCount.GetLowerBoundValue();
    const int32 RollCountMax = Table->RollCount.GetUpperBoundValue();
    const int32 NumRolls = Stream
        ? Stream->RandRange(RollCountMin, RollCountMax)
        : FMath::RandRange(RollCountMin, RollCountMax);

    TArray<FLootReward> Results;

    // Build requirement context once for all rolls in this table.
    // Requirements evaluate against the instigator's pawn using a default context.
    // Game-specific requirement types that need actor data should use a subclassed
    // FRequirementContext via the Make<T> factory. See DEVIATIONS.md.
    FRequirementContext ReqContext;

    for (int32 RollIdx = 0; RollIdx < NumRolls; ++RollIdx)
    {
        // Roll a value in [0.0, 1.0 + LuckBonus].
        const float LuckBonus = FMath::Max(0.0f, Context.LuckBonus);
        const float RolledValue = Stream
            ? Stream->FRandRange(0.0f, 1.0f + LuckBonus)
            : FMath::FRandRange(0.0f, 1.0f + LuckBonus);

        // Find the highest threshold entry that does not exceed RolledValue.
        // Entries are assumed sorted ascending by RollThreshold (IsDataValid auto-sorts).
        int32 CandidateIndex = INDEX_NONE;
        for (int32 i = Table->Entries.Num() - 1; i >= 0; --i)
        {
            if (Table->Entries[i].RollThreshold <= RolledValue)
            {
                CandidateIndex = i;
                break;
            }
        }

        if (CandidateIndex == INDEX_NONE)
        {
            // Dead zone or all thresholds exceed rolled value — no reward this roll.
            continue;
        }

        // Downgrade walk: evaluate requirements, downgrade on failure if configured.
        while (CandidateIndex >= 0)
        {
            const FLootTableEntry& Candidate = Table->Entries[CandidateIndex];

            // Evaluate all requirements (AND logic, short-circuit on first failure).
            bool bPassed = true;
            for (const TObjectPtr<URequirement>& Req : Candidate.EntryRequirements)
            {
                if (Req && !Req->Evaluate(ReqContext).bPassed)
                {
                    bPassed = false;
                    break;
                }
            }

            if (bPassed)
            {
                break; // Requirements passed — use this entry.
            }
            else if (Candidate.bDowngradeOnRequirementFailed)
            {
                --CandidateIndex; // Walk down to next lower entry.
            }
            else
            {
                CandidateIndex = INDEX_NONE; // No reward this roll.
                break;
            }
        }

        if (CandidateIndex < 0) CandidateIndex = INDEX_NONE;
        if (CandidateIndex == INDEX_NONE) continue;

        const FLootTableEntry& SelectedEntry = Table->Entries[CandidateIndex];

        // Warn in non-shipping builds if both Reward and NestedTable are configured.
        // NestedTable takes priority.
        ensureMsgf(
            !SelectedEntry.Reward.RewardType.IsValid() || SelectedEntry.NestedTable.IsNull(),
            TEXT("LootTableEntry at threshold %.2f has both Reward and NestedTable set. "
                 "NestedTable takes priority. Asset: %s"),
            SelectedEntry.RollThreshold,
            *GetNameSafe(Table));

        if (!SelectedEntry.NestedTable.IsNull())
        {
            // Recurse into nested table (sync load — safe when asset is already resident).
            ULootTable* NestedTable = SelectedEntry.NestedTable.LoadSynchronous();
            if (NestedTable)
            {
                TArray<FLootReward> SubResults =
                    RollTableInternal(NestedTable, Context, CurrentDepth + 1, Stream);
                Results.Append(MoveTemp(SubResults));
            }
        }
        else
        {
            // Resolve quantity and build the output reward.
            const int32 Quantity = ResolveQuantity(
                SelectedEntry.Quantity,
                SelectedEntry.QuantityDistribution,
                Stream);

            FLootReward Reward;
            Reward.RewardType       = SelectedEntry.Reward.RewardType;
            Reward.RewardDefinition = SelectedEntry.Reward.RewardDefinition;
            Reward.Quantity         = Quantity;
            Results.Add(MoveTemp(Reward));
        }
    }

    return Results;
}

int32 ULootRollerSubsystem::ResolveQuantity(
    FInt32Range            Range,
    EQuantityDistribution  Distribution,
    FRandomStream*         Stream)
{
    const int32 Min = Range.GetLowerBoundValue();
    const int32 Max = Range.GetUpperBoundValue();
    if (Min == Max) return Min;

    auto Roll = [&]() -> int32
    {
        return Stream ? Stream->RandRange(Min, Max) : FMath::RandRange(Min, Max);
    };

    switch (Distribution)
    {
    case EQuantityDistribution::Uniform:
        return Roll();

    case EQuantityDistribution::Normal:
        // Triangular approximation: average of two uniform rolls.
        // Biases toward midpoint, no external library dependency.
        return FMath::RoundToInt((Roll() + Roll()) * 0.5f);
    }

    // Fallback (unreachable with current enum values).
    return Min;
}
