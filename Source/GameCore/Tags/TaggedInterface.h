// TaggedInterface.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "TaggedInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType, NotBlueprintable)
class GAMECORE_API UTaggedInterface : public UInterface { GENERATED_BODY() };

class GAMECORE_API ITaggedInterface
{
    GENERATED_BODY()

public:
    // ── Primary Query (C++ hot path) ──────────────────────────────────────────

    // Returns the actor's full tag container by const reference.
    // Pure virtual C++ — intentionally not a UFUNCTION.
    // Called by GameCore systems on the resolution hot path (every scan period,
    // every server validation). No virtual dispatch overhead beyond the one call,
    // no Blueprint thunk, no copy.
    // Implementations must return a stable reference valid for the actor's lifetime.
    virtual const FGameplayTagContainer& GetGameplayTags() const = 0;

    // ── Convenience Queries (Blueprint + C++) ─────────────────────────────────

    // Returns true if the actor has the given tag.
    // Default implementation delegates to GetGameplayTags() — override only if
    // the backing store offers a faster single-tag lookup.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tags")
    bool HasGameplayTag(FGameplayTag Tag) const;
    virtual bool HasGameplayTag_Implementation(FGameplayTag Tag) const
    { return GetGameplayTags().HasTag(Tag); }

    // Returns true if the actor has ALL tags in the container.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tags")
    bool HasAllGameplayTags(const FGameplayTagContainer& Tags) const;
    virtual bool HasAllGameplayTags_Implementation(const FGameplayTagContainer& Tags) const
    { return GetGameplayTags().HasAll(Tags); }

    // Returns true if the actor has ANY tag in the container.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tags")
    bool HasAnyGameplayTags(const FGameplayTagContainer& Tags) const;
    virtual bool HasAnyGameplayTags_Implementation(const FGameplayTagContainer& Tags) const
    { return GetGameplayTags().HasAny(Tags); }

    // ── Mutation (Authority-only by convention) ───────────────────────────────

    // Add a tag to this actor's container.
    // AUTHORITY ONLY. Callers must check HasAuthority() before calling.
    // Implementations are responsible for replication — the interface makes no
    // guarantee about when or whether the change reaches clients.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tags")
    void AddGameplayTag(FGameplayTag Tag);
    virtual void AddGameplayTag_Implementation(FGameplayTag Tag) {}

    // Remove a tag from this actor's container.
    // AUTHORITY ONLY. Same replication contract as AddGameplayTag.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tags")
    void RemoveGameplayTag(FGameplayTag Tag);
    virtual void RemoveGameplayTag_Implementation(FGameplayTag Tag) {}
};
