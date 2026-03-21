# UJournalCollectionDefinition

**Files:** `GameCore/Source/GameCore/Journal/JournalCollectionDefinition.h` / `.cpp`  
**Type:** `UPrimaryDataAsset`

Designer-authored DataAsset defining a named set of collectable journal entries. Progress (found vs total) is always **derived at runtime** from `ClientAcquiredSet` — never stored per player.

---

## Class Definition

```cpp
// JournalCollectionDefinition.h
UCLASS(BlueprintType)
class GAMECORE_API UJournalCollectionDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    // Unique tag for this collection.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Collection",
              meta=(Categories="Journal.Collection"))
    FGameplayTag CollectionTag;

    // Display name shown in UI.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Collection")
    FText CollectionName;

    // Track this collection belongs to. Used to filter collections per tab.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Collection",
              meta=(Categories="Journal.Track"))
    FGameplayTag TrackTag;

    // All possible entries in this collection.
    // meta MustImplement filters the asset picker to IJournalEntry implementors only.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Collection",
              meta=(MustImplement="/Script/GameCore.JournalEntry"))
    TArray<TSoftObjectPtr<UJournalEntryDataAsset>> Members;

    // Optional nested sub-collections.
    // Progress of this collection = own Members + union of all SubCollections.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Collection")
    TArray<TSoftObjectPtr<UJournalCollectionDefinition>> SubCollections;

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;
    // Validates: CollectionTag set, TrackTag set, no circular sub-collection refs.
    // Circular ref detection: depth-first traversal with a TSet<FGameplayTag> visited guard.
#endif
};
```

---

## Editor Behavior

UE5's Details panel renders `TArray<TSoftObjectPtr<UJournalCollectionDefinition>>` as a standard array of asset pickers, filtered to `UJournalCollectionDefinition` assets. Designers build arbitrarily deep hierarchies.

**Example hierarchy:**
```
Journal.Collection.AllLore
  SubCollections:
    Journal.Collection.BooksOfDarkness
      Members: [Page1, Page2, Page3, Page4]
    Journal.Collection.ScrollsOfFire
      Members: [ScrollA, ScrollB]
  Members: [StandaloneEntry]

Progress of AllLore: Found = count(acquired ∩ {Page1..Page4, ScrollA, ScrollB, StandaloneEntry})
                     Total = 7
```

---

## Notes

- `PrimaryAssetType` for Asset Manager: `"JournalCollection"`.
- Sub-collections are loaded synchronously at `UJournalRegistrySubsystem::Initialize()` — they are lightweight (only tags + asset refs, no textures).
- Circular ref protection **must** be added to runtime traversal (`CollectMemberTags`, `ComputeProgress`) via a `TSet<FGameplayTag> Visited` stack guard, independent of the editor-only `IsDataValid` check. See Known Issues in Architecture.md.
