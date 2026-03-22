// GameplayTagComponent.cpp

#include "Tags/GameplayTagComponent.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

// ── Replication ───────────────────────────────────────────────────────────────

void UGameplayTagComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    FDoRepLifetimeParams Params;
    Params.bIsPushBased = true;
    Params.Condition = COND_None;  // Replicate to all relevant clients
    DOREPLIFETIME_WITH_PARAMS_FAST(UGameplayTagComponent, Tags, Params);
}

// ── OnRep ─────────────────────────────────────────────────────────────────────

void UGameplayTagComponent::OnRep_Tags()
{
    // CLIENTS ONLY — called by the replication system after a new container state arrives.
    // Per-tag OnTagChanged is NOT fired here — the rep callback only delivers the final
    // container, not a diff. Clients that need per-tag resolution must diff against a
    // locally cached snapshot inside their OnTagsChanged binding.
    OnTagsChanged.Broadcast(Tags);
}

// ── ITaggedInterface — Mutation ───────────────────────────────────────────────

void UGameplayTagComponent::AddGameplayTag_Implementation(FGameplayTag Tag)
{
    if (Tags.HasTagExact(Tag)) return;
    Tags.AddTag(Tag);
    MARK_PROPERTY_DIRTY_FROM_NAME(UGameplayTagComponent, Tags, this);
    OnTagChanged.Broadcast(Tag, 1, Tags);
    OnTagsChanged.Broadcast(Tags);
}

void UGameplayTagComponent::RemoveGameplayTag_Implementation(FGameplayTag Tag)
{
    if (!Tags.HasTagExact(Tag)) return;
    Tags.RemoveTag(Tag);
    MARK_PROPERTY_DIRTY_FROM_NAME(UGameplayTagComponent, Tags, this);
    OnTagChanged.Broadcast(Tag, 0, Tags);
    OnTagsChanged.Broadcast(Tags);
}

// ── Bulk Mutation ─────────────────────────────────────────────────────────────

void UGameplayTagComponent::SetGameplayTags(const FGameplayTagContainer& NewTags)
{
    // Diff before overwrite so we can fire accurate per-tag events.
    FGameplayTagContainer Added;
    FGameplayTagContainer Removed;

    for (const FGameplayTag& OldTag : Tags)
        if (!NewTags.HasTagExact(OldTag)) Removed.AddTag(OldTag);

    for (const FGameplayTag& NewTag : NewTags)
        if (!Tags.HasTagExact(NewTag)) Added.AddTag(NewTag);

    Tags = NewTags;
    MARK_PROPERTY_DIRTY_FROM_NAME(UGameplayTagComponent, Tags, this);

    // SERVER only: per-tag events fired for each delta tag.
    for (const FGameplayTag& Tag : Removed) OnTagChanged.Broadcast(Tag, 0, Tags);
    for (const FGameplayTag& Tag : Added)   OnTagChanged.Broadcast(Tag, 1, Tags);

    // SERVER + CLIENTS (via rep): full container notification.
    OnTagsChanged.Broadcast(Tags);
}
