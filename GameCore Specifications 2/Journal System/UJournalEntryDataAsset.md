# UJournalEntryDataAsset

**Files:** `GameCore/Source/GameCore/Journal/JournalEntryDataAsset.h` / `.cpp`  
**Type:** `UPrimaryDataAsset` + `IJournalEntry`  
**Abstract:** Yes — concrete subclasses live in the **game module**

Abstract base class for all journal content assets. Holds the `FGameplayTag` identity fields that are required by the system; subclasses add game-specific content fields.

---

## Class Definition

```cpp
// JournalEntryDataAsset.h
UCLASS(Abstract, BlueprintType)
class GAMECORE_API UJournalEntryDataAsset
    : public UPrimaryDataAsset
    , public IJournalEntry
{
    GENERATED_BODY()
public:
    // Unique tag identifying this entry type.
    // For non-repeatable entries: globally unique.
    // For repeatable entries: same tag appears multiple times with different timestamps.
    // Must match the EntryTag passed to UJournalComponent::AddEntry().
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
    // Does NOT validate uniqueness globally — that is a project convention.
#endif
};
```

---

## Concrete Example — Book Entry (Game Module)

```cpp
// Game module: BookJournalEntryDataAsset.h
UCLASS(BlueprintType)
class UBookJournalEntryDataAsset : public UJournalEntryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Book")
    FText Title;

    // Authored in editor with UE Rich Text markup.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Book")
    FText RichText;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Book")
    TSoftObjectPtr<UTexture2D> CoverImage;

    // IJournalEntry
    virtual FText GetEntryTitle_Implementation() const override { return Title; }
    virtual FGameplayTag GetTrackTag_Implementation() const override { return TrackTag; }
    virtual void BuildDetails(TFunction<void(FJournalRenderedDetails)> OnReady) const override
    {
        // Books are fully authored — no async load needed beyond this asset itself.
        FJournalRenderedDetails Out;
        Out.RichBodyText = RichText;
        Out.HeaderImage  = CoverImage;
        OnReady(Out);
    }
};
```

## Concrete Example — Quest Entry with Async Load (Game Module)

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

    virtual FText GetEntryTitle_Implementation() const override
    {
        // Title is available synchronously from the quest definition if already loaded,
        // otherwise provide a fallback from a local FText field.
        if (const UQuestDefinition* Def = QuestDefinition.Get())
            return Def->QuestTitle;
        return NSLOCTEXT("Journal", "QuestFallback", "Quest");
    }

    virtual FGameplayTag GetTrackTag_Implementation() const override { return TrackTag; }

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

## Notes

- The `PrimaryAssetType` for Asset Manager registration is `"JournalEntry"`. GameCore does not define a `GetPrimaryAssetId()` override — it relies on the default UPrimaryDataAsset behavior (asset name as ID within the type).
- `EntryTag` and `TrackTag` are the only system-required fields. All other fields are game-module concerns.
- `BuildDetails` is always called on the **owning client** from UI code. Server never calls it.
