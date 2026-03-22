// GameplayTagComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Tags/TaggedInterface.h"
#include "GameplayTagComponent.generated.h"

// Fires when a specific tag is added to or removed from the container.
// Mirrors GAS's RegisterGameplayTagEvent pattern for consistent usage across both systems.
//
// AUTHORITY + CLIENT (add/remove): Fires on the SERVER immediately when AddGameplayTag
// or RemoveGameplayTag is called. Also fires on the LOCAL OWNER if it calls mutation
// directly (non-replicated path). Does NOT fire on non-owning clients — they receive
// only OnTagsChanged via OnRep_Tags. See Section 6 (Known Limitations).
//
// NewCount: 1 when the tag was added, 0 when removed.
// UpdatedContainer: the full container state after the change.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnGameplayTagChanged,
    FGameplayTag,                   Tag,
    int32,                          NewCount,
    const FGameplayTagContainer&,   UpdatedContainer);

// Fires whenever the container changes for any reason (add, remove, or bulk SetGameplayTags).
//
// SERVER: fires immediately on every mutation.
// CLIENTS: fires via OnRep_Tags when the replicated container arrives.
//          All clients receive this — use it as the reliable cross-machine notification.
// Delivers the full updated container. Diff against a cached snapshot if per-tag
// resolution is needed on the client side.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameplayTagContainerChanged,
    const FGameplayTagContainer&, UpdatedContainer);

UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UGameplayTagComponent : public UActorComponent, public ITaggedInterface
{
    GENERATED_BODY()

    // The authoritative tag container.
    // Replicated via Push Model — only serialized when actually dirty.
    UPROPERTY(ReplicatedUsing = OnRep_Tags, EditAnywhere, BlueprintReadOnly, Category = "Tags")
    FGameplayTagContainer Tags;

    UFUNCTION()
    void OnRep_Tags();

public:
    // ── Replication ───────────────────────────────────────────────────────────
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // ── ITaggedInterface ──────────────────────────────────────────────────────

    virtual const FGameplayTagContainer& GetGameplayTags() const override { return Tags; }

    virtual bool HasGameplayTag_Implementation(FGameplayTag Tag) const override
    { return Tags.HasTag(Tag); }

    virtual bool HasAllGameplayTags_Implementation(const FGameplayTagContainer& InTags) const override
    { return Tags.HasAll(InTags); }

    virtual bool HasAnyGameplayTags_Implementation(const FGameplayTagContainer& InTags) const override
    { return Tags.HasAny(InTags); }

    // Add a single tag. No-op if already present (avoids unnecessary dirty mark + replication).
    // Fires OnTagChanged (NewCount=1) and OnTagsChanged on the SERVER immediately.
    // Non-owning CLIENTS receive OnTagsChanged only via OnRep_Tags — NOT OnTagChanged.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Tags")
    virtual void AddGameplayTag_Implementation(FGameplayTag Tag) override;

    // Remove a single tag. No-op if not present (avoids unnecessary dirty mark + replication).
    // Fires OnTagChanged (NewCount=0) and OnTagsChanged on the SERVER immediately.
    // Non-owning CLIENTS receive OnTagsChanged only via OnRep_Tags — NOT OnTagChanged.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Tags")
    virtual void RemoveGameplayTag_Implementation(FGameplayTag Tag) override;

    // ── Bulk Mutation ─────────────────────────────────────────────────────────

    // Replace the entire container in one operation.
    // More efficient than N Add/Remove calls — marks dirty once, one replication delta.
    // Diffs old vs. new to fire OnTagChanged correctly for each added/removed tag.
    // Fires OnTagChanged per changed tag (SERVER only) then OnTagsChanged once (SERVER + CLIENTS via rep).
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Tags")
    void SetGameplayTags(const FGameplayTagContainer& NewTags);

    // ── Delegates ─────────────────────────────────────────────────────────────

    // Per-tag delta event. Mirrors GAS's RegisterGameplayTagEvent pattern.
    // SERVER: fires on AddGameplayTag, RemoveGameplayTag, and SetGameplayTags.
    // CLIENTS: does NOT fire — clients receive OnTagsChanged via OnRep_Tags only.
    //          Client-side per-tag resolution: diff inside your OnTagsChanged binding.
    // NewCount: 1 = tag added, 0 = tag removed.
    UPROPERTY(BlueprintAssignable, Category = "Tags")
    FOnGameplayTagChanged OnTagChanged;

    // Full container changed event.
    // SERVER: fires on every mutation (add, remove, bulk set).
    // CLIENTS: fires via OnRep_Tags when replication delivers the updated container.
    //          This is the reliable cross-machine notification — bind here for client UI,
    //          client-side condition caches, and any logic that must run on all machines.
    UPROPERTY(BlueprintAssignable, Category = "Tags")
    FOnGameplayTagContainerChanged OnTagsChanged;
};
