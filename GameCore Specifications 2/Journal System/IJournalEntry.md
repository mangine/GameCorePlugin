# IJournalEntry

**Files:** `GameCore/Source/GameCore/Journal/JournalEntry.h` / `.cpp`  
**Type:** `UInterface` / `IJournalEntry`  
**Authority:** Client-only. Never called on the server.

---

## Purpose

The contract between data asset content and the UI layer. All `UJournalEntryDataAsset` subclasses implement this interface. Exposes metadata (title, track tag) synchronously and heavy content (rich text, textures) asynchronously via `BuildDetails()`.

---

## Declaration

```cpp
// JournalEntry.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "JournalTypes.h"
#include "JournalEntry.generated.h"

UIINTERFACE(MinimalAPI, BlueprintType)
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
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Journal")
    FText GetEntryTitle() const;

    /**
     * Top-level track tag for this entry.
     * Must match the TrackTag stored in FJournalEntryHandle at AddEntry time.
     * e.g. Journal.Track.Books
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Journal")
    FGameplayTag GetTrackTag() const;

    /**
     * Async content build. Implementations load heavy assets (textures, rich text
     * data tables, referenced definitions) and invoke OnReady when ready.
     * Called only for visible UI pages — never on the server.
     *
     * NOTE: This is a C++-only virtual — not a UFUNCTION. Blueprint subclasses
     * must implement GetEntryTitle and GetTrackTag; for BuildDetails they should
     * override in C++ only. See Architecture Known Issues #2.
     */
    virtual void BuildDetails(
        TFunction<void(FJournalRenderedDetails)> OnReady) const = 0;
};
```

---

## Notes

- `GetEntryTitle` and `GetTrackTag` are `BlueprintNativeEvent` — both C++ and Blueprint subclasses can override them.
- `BuildDetails` is a pure C++ virtual. It takes a `TFunction` because the result may arrive asynchronously (from `RequestAsyncLoad`). Implementors must always call `OnReady` exactly once.
- **Never called on the server.** Content loading would waste server RAM and asset loading budget.
- The interface is implemented by `UJournalEntryDataAsset` and all its concrete subclasses. `UJournalRegistrySubsystem::GetEntryAsset()` returns the asset; the caller casts to `IJournalEntry` to call `BuildDetails`.
