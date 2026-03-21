# IJournalEntry

**Files:** `GameCore/Source/GameCore/Journal/JournalEntry.h` / `.cpp`  
**Type:** `UInterface`

The contract between the data asset layer and the UI layer. Implemented by all journal data assets. **Client-only** — never called on the server.

---

## Interface Declaration

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
    //
    // WARNING: Caller must guard the callback with TWeakObjectPtr if the calling
    // widget can be destroyed before the async load completes. The callback holds
    // no widget reference — that is the caller's responsibility.
    virtual void BuildDetails(
        TFunction<void(FJournalRenderedDetails)> OnReady) const = 0;
};
```

---

## Notes

- `GetEntryTitle()` and `GetTrackTag()` are `BlueprintNativeEvent` so Blueprint subclasses in the game module can implement or override them.
- `BuildDetails()` is a pure C++ virtual (not UFUNCTION) because `TFunction` cannot be a Blueprint parameter. It is client-only and never needs Blueprint exposure.
- The interface has no server-side use. The server never loads entry assets — it only works with `FGameplayTag` identities.
- Concrete implementations live in the **game module**, not in GameCore. GameCore only defines the interface and abstract base.
