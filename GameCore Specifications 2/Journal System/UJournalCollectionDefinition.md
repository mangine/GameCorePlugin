# UJournalCollectionDefinition

**Files:** `GameCore/Source/GameCore/Journal/JournalCollectionDefinition.h` / `.cpp`  
**Type:** `UPrimaryDataAsset`  

---

## Purpose

Designer-authored DataAsset defining a named set of collectable journal entries. Progress (found vs total) is always **derived at runtime** from the player's `ClientAcquiredSet` — never stored per player.

Supports arbitrary nesting via `SubCollections`. Circular references are caught in `IsDataValid()`.

---

## Class Definition

```cpp
// JournalCollectionDefinition.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "JournalCollectionDefinition.generated.h"

class UJournalEntryDataAsset;

UCLASS(BlueprintType)
class GAMECORE_API UJournalCollectionDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    /**
     * Unique tag for this collection.
     * Used by UJournalRegistrySubsystem as the registry key.
     * Must be under Journal.Collection namespace.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Collection",
              meta=(Categories="Journal.Collection"))
    FGameplayTag CollectionTag;

    /** Localized display name shown in UI collection tabs. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Collection")
    FText CollectionName;

    /**
     * Track this collection belongs to.
     * Used to filter collections per journal tab.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Collection",
              meta=(Categories="Journal.Track"))
    FGameplayTag TrackTag;

    /**
     * All possible entries in this collection.
     * The asset picker is filtered to UJournalEntryDataAsset subclasses.
     * Progress = entries in this array whose EntryTag is in the player's AcquiredSet.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Collection",
              meta=(MustImplement="/Script/GameCore.JournalEntry"))
    TArray<TSoftObjectPtr<UJournalEntryDataAsset>> Members;

    /**
     * Optional nested sub-collections.
     * Progress of this collection = Members + union of all SubCollections (recursive).
     * UE5 renders this as a standard array of asset pickers.
     * MUST NOT form a cycle — validated in IsDataValid().
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Collection")
    TArray<TSoftObjectPtr<UJournalCollectionDefinition>> SubCollections;

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;
    // Validates:
    //   - CollectionTag is set
    //   - TrackTag is set
    //   - No circular sub-collection references (via visited-set DFS)
#endif

protected:
    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(
            FPrimaryAssetType(TEXT("JournalCollection")),
            CollectionTag.IsValid() ? CollectionTag.GetTagName() : GetFName());
    }
};
```

---

## `IsDataValid` Implementation

```cpp
// JournalCollectionDefinition.cpp
#if WITH_EDITOR
EDataValidationResult UJournalCollectionDefinition::IsDataValid(
    FDataValidationContext& Context) const
{
    EDataValidationResult Result = Super::IsDataValid(Context);

    if (!CollectionTag.IsValid())
    {
        Context.AddError(NSLOCTEXT("Journal", "NoCollTag",
            "JournalCollectionDefinition: CollectionTag must be set."));
        Result = EDataValidationResult::Invalid;
    }

    if (!TrackTag.IsValid())
    {
        Context.AddError(NSLOCTEXT("Journal", "NoTrackTagColl",
            "JournalCollectionDefinition: TrackTag must be set."));
        Result = EDataValidationResult::Invalid;
    }

    // Circular reference detection via DFS with visited set.
    TSet<FGameplayTag> Visited;
    Visited.Add(CollectionTag);
    TFunction<bool(const UJournalCollectionDefinition*)> CheckCycle =
        [&](const UJournalCollectionDefinition* Node) -> bool
    {
        for (const TSoftObjectPtr<UJournalCollectionDefinition>& Sub : Node->SubCollections)
        {
            const UJournalCollectionDefinition* SubDef = Sub.Get();
            if (!SubDef) continue;
            if (Visited.Contains(SubDef->CollectionTag))
            {
                Context.AddError(FText::Format(
                    NSLOCTEXT("Journal", "CircularSub",
                        "JournalCollectionDefinition: Circular sub-collection reference detected involving '{0}'."),
                    FText::FromString(SubDef->CollectionTag.ToString())));
                return true; // cycle found
            }
            Visited.Add(SubDef->CollectionTag);
            if (CheckCycle(SubDef)) return true;
            Visited.Remove(SubDef->CollectionTag);
        }
        return false;
    };
    if (CheckCycle(this))
        Result = EDataValidationResult::Invalid;

    return Result;
}
#endif
```

---

## Designer Notes

**Example hierarchy:**
```
DA_Collection_AllLore
  CollectionTag = Journal.Collection.AllLore
  TrackTag      = Journal.Track.Books
  Members       = [StandaloneEntry]
  SubCollections:
    DA_Collection_BooksOfDarkness
      Members: [Page1, Page2, Page3, Page4]
    DA_Collection_ScrollsOfFire
      Members: [ScrollA, ScrollB]
```

Progress of `AllLore`: Found = (acquired ∩ {StandaloneEntry, Page1–4, ScrollA–B}), Total = 7.

Nested sub-collections are fully supported to arbitrary depth. The UE5 editor renders `SubCollections` as a standard array of asset pickers filtered to `UJournalCollectionDefinition` assets.
