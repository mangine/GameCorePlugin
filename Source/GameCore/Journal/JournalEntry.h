// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Journal/JournalTypes.h"
#include "JournalEntry.generated.h"

// ============================================================================
// UJournalEntry / IJournalEntry
//
// Contract between data asset content and the UI layer.
// All UJournalEntryDataAsset subclasses implement this interface.
//
// Authority: Client-only. Never called on the server.
// ============================================================================

UINTERFACE(MinimalAPI, BlueprintType)
class GAMECORE_API UJournalEntry : public UInterface
{
    GENERATED_BODY()
};

class GAMECORE_API IJournalEntry
{
    GENERATED_BODY()
public:
    /**
     * Localized display title shown in list views and pagination.
     * Synchronous — no asset load beyond the data asset itself.
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Journal")
    FText GetEntryTitle() const;

    /**
     * Top-level track tag for this entry.
     * Must match the TrackTag stored in FJournalEntryHandle at AddEntry time.
     * e.g. Journal.Track.Books
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Journal")
    FGameplayTag GetTrackTag() const;

    /**
     * Async content build. Implementations load heavy assets (textures, rich text
     * data tables, referenced definitions) and invoke OnReady when ready.
     * Called only for visible UI pages — never on the server.
     *
     * NOTE: This is a C++-only virtual — not a UFUNCTION. Blueprint subclasses
     * must implement GetEntryTitle and GetTrackTag; for BuildDetails they should
     * override in C++ only. See Architecture Known Issues #2.
     *
     * Implementors MUST call OnReady exactly once.
     */
    virtual void BuildDetails(
        TFunction<void(FJournalRenderedDetails)> OnReady) const = 0;
};
