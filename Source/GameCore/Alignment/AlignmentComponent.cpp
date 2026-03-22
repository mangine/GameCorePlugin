// Copyright GameCore Plugin. All Rights Reserved.
#include "Alignment/AlignmentComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerState.h"
#include "EventBus/GameCoreEventBus.h"
#include "EventBus/GameCoreEventTags.h"
#include "Persistence/PersistenceRegistrationComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogAlignment, Log, All);

// =============================================================================
// Constructor
// =============================================================================

UAlignmentComponent::UAlignmentComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = false;
}

// =============================================================================
// Setup
// =============================================================================

void UAlignmentComponent::RegisterAlignment(UAlignmentDefinition* Definition)
{
    if (!ensure(GetOwner()->HasAuthority())) return;
    if (!ensure(Definition && Definition->AlignmentTag.IsValid())) return;

    const FGameplayTag Tag = Definition->AlignmentTag;

    if (Definitions.Contains(Tag))
    {
        return; // Idempotent — already registered.
    }

    Definitions.Add(Tag, Definition);

    // Add a runtime entry if not already present (Serialize_Load may have seeded the value).
    if (!AlignmentData.FindByTag(Tag))
    {
        FAlignmentEntry& NewEntry    = AlignmentData.Items.AddDefaulted_GetRef();
        NewEntry.AlignmentTag        = Tag;
        NewEntry.UnderlyingValue     = 0.f;
        NewEntry.EffectiveMin        = Definition->EffectiveMin;  // Cached for client queries
        NewEntry.EffectiveMax        = Definition->EffectiveMax;
        AlignmentData.MarkItemDirty(NewEntry);
    }
    else
    {
        // Entry was pre-seeded by Serialize_Load. Ensure range is up to date.
        FAlignmentEntry* Entry  = AlignmentData.FindByTag(Tag);
        Entry->EffectiveMin     = Definition->EffectiveMin;
        Entry->EffectiveMax     = Definition->EffectiveMax;
        AlignmentData.MarkItemDirty(*Entry);
    }
}

// =============================================================================
// Mutation
// =============================================================================

void UAlignmentComponent::ApplyAlignmentDeltas(
    const TArray<FAlignmentDelta>& Deltas,
    const FRequirementContext& Context)
{
    check(GetOwner()->HasAuthority());

    FAlignmentChangedMessage Msg;
    Msg.PlayerState = Cast<APlayerState>(GetOwner());

    for (const FAlignmentDelta& Delta : Deltas)
    {
        if (FMath::IsNearlyZero(Delta.Delta)) continue;

        UAlignmentDefinition* Def = Definitions.FindRef(Delta.AlignmentTag);
        if (!Def)
        {
            UE_LOG(LogAlignment, Warning,
                TEXT("ApplyAlignmentDeltas: tag '%s' not registered on %s — skipped."),
                *Delta.AlignmentTag.ToString(), *GetOwner()->GetName());
            continue;
        }

        if (Def->ChangeRequirements)
        {
            const FRequirementResult Result = Def->ChangeRequirements->Evaluate(Context);
            if (!Result.bPassed) continue;
        }

        FAlignmentEntry* Entry = AlignmentData.FindByTag(Delta.AlignmentTag);
        if (!Entry) continue; // Should not happen if Definitions and AlignmentData are in sync.

        const float OldUnderlying  = Entry->UnderlyingValue;
        Entry->UnderlyingValue     = FMath::Clamp(
            Entry->UnderlyingValue + Delta.Delta,
            Def->SaturatedMin,
            Def->SaturatedMax);

        const float AppliedDelta = Entry->UnderlyingValue - OldUnderlying;
        if (FMath::IsNearlyZero(AppliedDelta)) continue; // Already saturated in that direction.

        AlignmentData.MarkItemDirty(*Entry);

        FAlignmentChangedEntry& ChangeEntry = Msg.Changes.AddDefaulted_GetRef();
        ChangeEntry.AlignmentTag  = Delta.AlignmentTag;
        ChangeEntry.AppliedDelta  = AppliedDelta;
        ChangeEntry.NewUnderlying = Entry->UnderlyingValue;
        ChangeEntry.NewEffective  = Entry->GetEffectiveValue();
    }

    if (Msg.Changes.IsEmpty()) return;

    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        Bus->Broadcast(
            GameCoreEventTags::Alignment_Changed,
            MoveTemp(Msg),
            EGameCoreEventScope::ServerOnly);
    }

    NotifyDirty();
}

// =============================================================================
// Query
// =============================================================================

float UAlignmentComponent::GetEffectiveAlignment(FGameplayTag AlignmentTag) const
{
    const FAlignmentEntry* Entry = AlignmentData.FindByTag(AlignmentTag);
    if (!Entry) return 0.f;
    return Entry->GetEffectiveValue(); // Uses cached EffectiveMin/Max — works on client.
}

float UAlignmentComponent::GetUnderlyingAlignment(FGameplayTag AlignmentTag) const
{
    const FAlignmentEntry* Entry = AlignmentData.FindByTag(AlignmentTag);
    return Entry ? Entry->UnderlyingValue : 0.f;
}

bool UAlignmentComponent::IsAlignmentRegistered(FGameplayTag AlignmentTag) const
{
    return AlignmentData.FindByTag(AlignmentTag) != nullptr;
}

// =============================================================================
// Replication
// =============================================================================

void UAlignmentComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    // COND_OwnerOnly: only the owning client needs their own alignment data.
    DOREPLIFETIME_CONDITION(UAlignmentComponent, AlignmentData, COND_OwnerOnly);
}

void UAlignmentComponent::OnRep_AlignmentData()
{
    // GMS is NOT broadcast on the client.
    // Game UI binds to this delegate for refresh.
    OnAlignmentDataReplicated.Broadcast();
}

// =============================================================================
// IPersistableComponent
// =============================================================================

void UAlignmentComponent::Serialize_Save(FArchive& Ar)
{
    // Serialize only the tag + underlying value pairs.
    // EffectiveMin/Max is re-applied from the definition at RegisterAlignment time.
    int32 Count = AlignmentData.Items.Num();
    Ar << Count;
    for (FAlignmentEntry& Entry : AlignmentData.Items)
    {
        FGameplayTag Tag = Entry.AlignmentTag;
        Ar << Tag;
        Ar << Entry.UnderlyingValue;
    }
}

void UAlignmentComponent::Serialize_Load(FArchive& Ar, uint32 SavedVersion)
{
    // Called after RegisterAlignment so Definitions is already populated.
    // Silently ignores tags not currently registered (axis removed from game).
    int32 Count;
    Ar << Count;
    for (int32 i = 0; i < Count; ++i)
    {
        FGameplayTag Tag;
        float UnderlyingValue;
        Ar << Tag;
        Ar << UnderlyingValue;

        if (FAlignmentEntry* Entry = AlignmentData.FindByTag(Tag))
        {
            Entry->UnderlyingValue = UnderlyingValue;
            AlignmentData.MarkItemDirty(*Entry);
        }
        // Tag not registered — silently skip (axis was removed from the game).
    }
}

void UAlignmentComponent::ClearIfSaved(uint32 FlushedGeneration)
{
    if (DirtyGeneration <= FlushedGeneration)
        bDirty = false;
}

// =============================================================================
// NotifyDirty
// =============================================================================

void UAlignmentComponent::NotifyDirty()
{
    if (bDirty) return;
    if (!CachedRegComp.IsValid())
        CachedRegComp = GetOwner()->FindComponentByClass<UPersistenceRegistrationComponent>();
    if (CachedRegComp.IsValid())
    {
        DirtyGeneration = CachedRegComp->SaveGeneration;
        bDirty = true;
        CachedRegComp->MarkDirty();
    }
#if !UE_BUILD_SHIPPING
    else
    {
        UE_LOG(LogAlignment, Verbose,
            TEXT("UAlignmentComponent::NotifyDirty — no UPersistenceRegistrationComponent "
                 "found on %s. Alignment changes will not be persisted."),
            *GetOwner()->GetName());
    }
#endif
}
