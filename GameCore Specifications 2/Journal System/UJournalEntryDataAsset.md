# UJournalEntryDataAsset

**Files:** `GameCore/Source/GameCore/Journal/JournalEntryDataAsset.h` / `.cpp`  
**Type:** `UPrimaryDataAsset` + `IJournalEntry`  
**Abstract:** Yes — concrete subclasses live in the **game module**.

---

## Purpose

Abstract base class for all journal content assets. Provides the `EntryTag` and `TrackTag` identity fields that GameCore needs, plus editor validation. Concrete subclasses add game-specific content (rich text, textures, audio, quest references, etc.).

---

## Class Definition

```cpp
// JournalEntryDataAsset.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "JournalEntry.h"
#include "JournalEntryDataAsset.generated.h"

/**
 * Abstract base for all journal entry data assets.
 * Concrete subclasses belong in the game module — not in GameCore.
 *
 * Asset Manager primary asset type: "JournalEntry"
 * All instances must be placed under the directory scanned by Asset Manager.
 */
UCLASS(Abstract, BlueprintType)
class GAMECORE_API UJournalEntryDataAsset
    : public UPrimaryDataAsset
    , public IJournalEntry
{
    GENERATED_BODY()
public:
    /**
     * Unique tag identifying this entry type.
     * - Must be unique per non-repeatable entry.
     * - Must match the EntryTag passed to UJournalComponent::AddEntry().
     * - Must be under the Journal.Entry namespace.
     * Validated in IsDataValid().
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Journal",
              meta=(Categories="Journal.Entry"))
    FGameplayTag EntryTag;

    /**
     * Track this entry belongs to.
     * Must match the TrackTag passed to UJournalComponent::AddEntry().
     * e.g. Journal.Track.Books
     * Validated in IsDataValid().
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Journal",
              meta=(Categories="Journal.Track"))
    FGameplayTag TrackTag;

    // IJournalEntry — GetTrackTag default implementation returns TrackTag.
    virtual FGameplayTag GetTrackTag_Implementation() const override { return TrackTag; }

    // GetEntryTitle has no default — concrete subclasses must implement.

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;
    // Validates:
    //   - EntryTag is set (not empty)
    //   - TrackTag is set (not empty)
#endif

protected:
    // Override GetPrimaryAssetId to use EntryTag as the asset name portion.
    // This keeps Asset Manager IDs stable and human-readable.
    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(
            FPrimaryAssetType(TEXT("JournalEntry")),
            EntryTag.IsValid() ? EntryTag.GetTagName() : GetFName());
    }
};
```

---

## `IsDataValid` Implementation

```cpp
// JournalEntryDataAsset.cpp
#if WITH_EDITOR
EDataValidationResult UJournalEntryDataAsset::IsDataValid(
    FDataValidationContext& Context) const
{
    EDataValidationResult Result = Super::IsDataValid(Context);

    if (!EntryTag.IsValid())
    {
        Context.AddError(NSLOCTEXT("Journal", "NoEntryTag",
            "JournalEntryDataAsset: EntryTag must be set."));
        Result = EDataValidationResult::Invalid;
    }

    if (!TrackTag.IsValid())
    {
        Context.AddError(NSLOCTEXT("Journal", "NoTrackTag",
            "JournalEntryDataAsset: TrackTag must be set."));
        Result = EDataValidationResult::Invalid;
    }

    return Result;
}
#endif
```

---

## Concrete Subclass Examples (Game Module)

### Book Entry

```cpp
// BookJournalEntryDataAsset.h  (game module)
UCLASS(BlueprintType)
class UBookJournalEntryDataAsset : public UJournalEntryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Book")
    FText Title;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Book",
              meta=(MultiLine=true))
    FText RichText;  // Authored with UE Rich Text markup in the editor

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Book")
    TSoftObjectPtr<UTexture2D> CoverImage;

    // IJournalEntry
    virtual FText GetEntryTitle_Implementation() const override { return Title; }
    virtual FGameplayTag GetTrackTag_Implementation() const override { return TrackTag; }

    virtual void BuildDetails(TFunction<void(FJournalRenderedDetails)> OnReady) const override
    {
        // Book content is fully inline — no async load needed.
        FJournalRenderedDetails Out;
        Out.RichBodyText = RichText;
        Out.HeaderImage  = CoverImage;
        OnReady(Out);
    }
};
```

### Quest Entry (Async Load)

```cpp
// QuestJournalEntryDataAsset.h  (game module)
UCLASS(BlueprintType)
class UQuestJournalEntryDataAsset : public UJournalEntryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Quest")
    TSoftObjectPtr<UQuestDefinition> QuestDefinition;

    virtual FText GetEntryTitle_Implementation() const override
    {
        // Title from the quest definition — may require it to be loaded.
        // Keep a cached FText Title property if you need title without async load.
        if (const UQuestDefinition* Def = QuestDefinition.Get())
            return Def->Title;
        return NSLOCTEXT("Journal", "UnknownQuest", "Unknown Quest");
    }

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
