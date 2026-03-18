# Data Assets and Definitions

**Sub-page of:** [Journal System](../Journal%20System.md)

---

## `FJournalEntryHandle`

The atomic unit of the journal. Stored in the persisted array and sent to the client. Contains no content — only identity and acquisition time.

```cpp
// JournalTypes.h
USTRUCT(BlueprintType)
struct GAMECORE_API FJournalEntryHandle
{
    GENERATED_BODY()

    // Unique identity of this entry type. Maps to a UJournalEntryDataAsset
    // via UJournalRegistrySubsystem. Replicates as a network index — never a string.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag EntryTag;

    // Top-level track. Used for tab filtering on the client.
    // e.g. Journal.Track.Books | Journal.Track.Adventure
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag TrackTag;

    // Server UTC unix epoch at time of acquisition.
    UPROPERTY(BlueprintReadOnly)
    int64 AcquiredTimestamp = 0;
};
```

> **No `FGuid` per handle.** Identity is `EntryTag`. For repeating entries (daily quests), the same `EntryTag` appears multiple times with different `AcquiredTimestamp` values. Uniqueness per acquisition event is not needed — timestamp is sufficient for sort and display.

> **No asset reference in the handle.** `EntryTag` → asset is resolved via `UJournalRegistrySubsystem::GetEntryAsset()` on demand. This keeps the handle small and avoids string paths in replication.

---

## `IJournalEntry` Interface

All journal data assets implement this interface. The interface is the contract between the data asset layer and the UI layer. It is **client-only** — never called on the server.

```cpp
// JournalEntry.h
UIINTERFACE(MinimalAPI, BlueprintType)
class GAMECORE_API UJournalEntry : public UInterface
{
    GENERATED_BODY()
};

class GAMECORE_API IJournalEntry
{
    GENERATED_BODY()
public:
    // Localized display title shown in list views and pagination.
    // Sync — no asset load required beyond the data asset itself.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Journal")
    FText GetEntryTitle() const;

    // Top-level track tag. Must match the TrackTag stored in FJournalEntryHandle.
    // e.g. Journal.Track.Books
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Journal")
    FGameplayTag GetTrackTag() const;

    // Async content build. Implementations load heavy assets (textures, rich text
    // data tables) and fire OnReady when ready. Called only for visible UI pages.
    // Never called on the server.
    virtual void BuildDetails(
        TFunction<void(FJournalRenderedDetails)> OnReady) const = 0;
};
```

---

## `FJournalRenderedDetails`

Output of `BuildDetails()`. Contains everything the UI needs to render one entry's content panel.

```cpp
// JournalTypes.h
USTRUCT(BlueprintType)
struct GAMECORE_API FJournalRenderedDetails
{
    GENERATED_BODY()

    // UE Rich Text markup string. Supports inline images, font styles,
    // color runs, and custom decorators via URichTextBlock + UDataTable.
    UPROPERTY(BlueprintReadOnly)
    FText RichBodyText;

    // Optional header image displayed above body text.
    UPROPERTY(BlueprintReadOnly)
    TSoftObjectPtr<UTexture2D> HeaderImage;

    // Extend here as needed: voice line cue, ambient audio, etc.
};
```

---

## `UJournalEntryDataAsset`

Abstract base class for all journal content assets. Lives in GameCore. Concrete subclasses live in the **game module**.

```cpp
// JournalEntryDataAsset.h
UCLASS(Abstract, BlueprintType)
class GAMECORE_API UJournalEntryDataAsset
    : public UPrimaryDataAsset
    , public IJournalEntry
{
    GENERATED_BODY()
public:
    // Unique tag identifying this entry. Must be unique per non-repeatable entry.
    // Must match the EntryTag in FJournalEntryHandle at AddEntry time.
    // Validated in IsDataValid().
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Journal",
              meta=(Categories="Journal.Entry"))
    FGameplayTag EntryTag;

    // Track this entry belongs to. Must match the TrackTag passed to AddEntry.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Journal",
              meta=(Categories="Journal.Track"))
    FGameplayTag TrackTag;

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;
    // Validates: EntryTag is set, TrackTag is set.
#endif
};
```

### Concrete Example — Game Module

```cpp
// Game module: BookJournalEntryDataAsset.h
UCLASS(BlueprintType)
class UBookJournalEntryDataAsset : public UJournalEntryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Book")
    FText Title;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Book")
    FText RichText;  // Authored in editor with Rich Text markup

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Book")
    TSoftObjectPtr<UTexture2D> CoverImage;

    // IJournalEntry
    virtual FText GetEntryTitle_Implementation() const override { return Title; }
    virtual FGameplayTag GetTrackTag_Implementation() const override { return TrackTag; }
    virtual void BuildDetails(TFunction<void(FJournalRenderedDetails)> OnReady) const override
    {
        // Books are fully authored — no async load needed beyond this asset itself.
        // Asset is already loaded when BuildDetails is called (it IS the asset).
        FJournalRenderedDetails Out;
        Out.RichBodyText  = RichText;
        Out.HeaderImage   = CoverImage;
        OnReady(Out);
    }
};
```

### Concrete Example — Quest Entry (async load)

```cpp
// Game module: QuestJournalEntryDataAsset.h
UCLASS(BlueprintType)
class UQuestJournalEntryDataAsset : public UJournalEntryDataAsset
{
    GENERATED_BODY()
public:
    // Soft ref to the quest definition — loaded async on demand.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest")
    TSoftObjectPtr<UQuestDefinition> QuestDefinition;

    virtual void BuildDetails(TFunction<void(FJournalRenderedDetails)> OnReady) const override
    {
        UAssetManager::Get().GetStreamableManager().RequestAsyncLoad(
            QuestDefinition.ToSoftObjectPath(),
            [this, OnReady]()
            {
                const UQuestDefinition* Def = QuestDefinition.Get();
                FJournalRenderedDetails Out;
                Out.RichBodyText = Def ? Def->SummaryText : FText::GetEmpty();
                Out.HeaderImage  = Def ? Def->QuestImage  : nullptr;
                OnReady(Out);
            });
    }
};
```

---

## `UJournalCollectionDefinition`

Designer-authored DataAsset defining a named set of collectable journal entries. Progress (found vs total) is always derived at runtime — never stored per player.

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
    // MustImplement filters the asset picker to UJournalEntryDataAsset subclasses only.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Collection",
              meta=(MustImplement="/Script/GameCore.JournalEntry"))
    TArray<TSoftObjectPtr<UJournalEntryDataAsset>> Members;

    // Optional nested sub-collections.
    // Progress of this collection = own Members + union of all SubCollections.
    // UE editor supports asset picker for DataAsset references — fully authoring-friendly.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Collection")
    TArray<TSoftObjectPtr<UJournalCollectionDefinition>> SubCollections;

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;
    // Validates: CollectionTag set, TrackTag set, no circular sub-collection refs.
#endif
};
```

### Collections of Collections — Editor Behavior

UE5's Details panel renders `TArray<TSoftObjectPtr<UJournalCollectionDefinition>>` as a standard array of asset pickers, filtered to `UJournalCollectionDefinition` assets. Designers build arbitrarily deep hierarchies by assigning sub-collection assets. There is no engine-level depth limit, but circular references must be caught in `IsDataValid`.

**Example hierarchy:**
```
Journal.Collection.AllLore
  SubCollections:
    Journal.Collection.BooksOfDarkness
      Members: [Page1, Page2, Page3, Page4]
    Journal.Collection.ScrollsOfFire
      Members: [ScrollA, ScrollB]
  Members: [StandaloneEntry]
```

Progress of `AllLore`: found = sum of found across all members recursively, total = 7.
