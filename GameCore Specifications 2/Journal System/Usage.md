# Journal System — Usage Guide

---

## Minimum Viable Integration Checklist

```
☐ GameCore plugin enabled in .uproject
☐ UJournalComponent added to APlayerState constructor
☐ UPersistenceRegistrationComponent present on APlayerState (shared with other components)
☐ DefaultGame.ini: JournalEntry and JournalCollection primary asset types registered
☐ DefaultGameplayTags.ini: tags from Architecture.md copied to plugin and game module ini files
☐ At least one UJournalEntryDataAsset subclass created in game module
☐ UJournalEntryDataAsset subclasses placed in scanned directory (/Game/Journal/Entries)
☐ UJournalCollectionDefinition assets created in /Game/Journal/Collections
☐ A bridge component (or equivalent) on APlayerState subscribes to events and calls AddEntry
☐ UI binds to OnJournalSynced and OnEntryAdded delegates
```

---

## 1 — APlayerState Setup

```cpp
// YourPlayerState.h
UPROPERTY()
TObjectPtr<UJournalComponent> JournalComponent;

// YourPlayerState.cpp constructor
JournalComponent = CreateDefaultSubobject<UJournalComponent>(TEXT("JournalComponent"));
// UPersistenceRegistrationComponent is shared — already present for other persistable components.
// JournalComponent registers itself through GetPersistenceKey() automatically.
```

---

## 2 — Asset Manager Configuration

```ini
; DefaultGame.ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(
    PrimaryAssetType="JournalEntry",
    AssetBaseClass=/Script/GameCore.JournalEntryDataAsset,
    bHasBlueprintClasses=True,
    bIsEditorOnly=False,
    Directories=((Path="/Game/Journal/Entries"))
)
+PrimaryAssetTypesToScan=(
    PrimaryAssetType="JournalCollection",
    AssetBaseClass=/Script/GameCore.JournalCollectionDefinition,
    bHasBlueprintClasses=False,
    bIsEditorOnly=False,
    Directories=((Path="/Game/Journal/Collections"))
)
```

---

## 3 — Create Entry Asset Subclasses (Game Module)

Subclass `UJournalEntryDataAsset` for each entry type:

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
    FText RichText;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Book")
    TSoftObjectPtr<UTexture2D> CoverImage;

    // IJournalEntry
    virtual FText GetEntryTitle_Implementation() const override { return Title; }
    virtual FGameplayTag GetTrackTag_Implementation() const override { return TrackTag; }
    virtual void BuildDetails(TFunction<void(FJournalRenderedDetails)> OnReady) const override
    {
        // Books are fully authored — asset is already loaded. No async needed.
        FJournalRenderedDetails Out;
        Out.RichBodyText = RichText;
        Out.HeaderImage  = CoverImage;
        OnReady(Out);
    }
};
```

Set on each asset in the editor:
- `EntryTag` — unique `Journal.Entry.*` tag
- `TrackTag` — e.g. `Journal.Track.Books`

---

## 4 — Create Collection Assets (Game Module)

In the Content Browser at `/Game/Journal/Collections/`:

```
JC_BooksOfDarkness    CollectionTag=Journal.Collection.BooksOfDarkness
  TrackTag=Journal.Track.Books
  Members: [BP_BookEntry_Page1, BP_BookEntry_Page2, BP_BookEntry_Page3]

JC_AllLore            CollectionTag=Journal.Collection.AllLore
  TrackTag=Journal.Track.Books
  SubCollections: [JC_BooksOfDarkness, JC_ScrollsOfFire]
  Members: [BP_BookEntry_StandaloneEntry]
```

---

## 5 — Wire the Bridge (Game Module)

Create a bridge component on `APlayerState` that subscribes to `UGameCoreEventBus` events and calls `AddEntry`:

```cpp
// UJournalTrackerBridge : public UActorComponent (game module)
void UJournalTrackerBridge::BeginPlay()
{
    Super::BeginPlay();
    if (!GetOwner()->HasAuthority()) return;

    auto* EventBus = UGameCoreEventBus::Get(this);
    auto* Journal  = GetOwner()->FindComponentByClass<UJournalComponent>();
    if (!EventBus || !Journal) return;

    // Quest completed → journal entry
    QuestHandle = EventBus->StartListening<FQuestCompletedMessage>(
        TAG_GameCoreEvent_Quest_Completed,
        [Journal](FGameplayTag, const FQuestCompletedMessage& Msg)
        {
            // Only react to quests for this player.
            if (Journal->GetOwner() == Msg.PlayerState)
            {
                Journal->AddEntry(
                    Msg.QuestCompletedTag,
                    TAG_Journal_Track_Adventure,
                    true); // repeatable quests allowed
            }
        });

    // Place discovered → non-repeatable journal entry
    PlaceHandle = EventBus->StartListening<FPlaceDiscoveredMessage>(
        TAG_GameCoreEvent_World_PlaceDiscovered,
        [Journal](FGameplayTag, const FPlaceDiscoveredMessage& Msg)
        {
            if (Journal->GetOwner() == Msg.PlayerState)
            {
                Journal->AddEntry(
                    Msg.PlaceTag,
                    TAG_Journal_Track_Adventure,
                    false); // places are non-repeatable
            }
        });
}

void UJournalTrackerBridge::EndPlay(const EEndPlayReason::Type Reason)
{
    // Always unregister to avoid dangling lambda in the event bus.
    if (auto* EventBus = UGameCoreEventBus::Get(this))
    {
        EventBus->StopListening(QuestHandle);
        EventBus->StopListening(PlaceHandle);
    }
    Super::EndPlay(Reason);
}
```

> **Do not call `AddEntry` directly from external game code** — route all journal mutations through an event-driven bridge. This keeps the journal decoupled from game-specific systems.

---

## 6 — Calling AddEntry Directly (Server Authority)

For simple cases where an event-bus bridge is overkill:

```cpp
// Server-side only. APlayerState must have authority.
void AGiveLootInteraction::Execute(APlayerState* Target)
{
    // Give item...

    // Record loot discovery in journal.
    if (UJournalComponent* Journal = Target->FindComponentByClass<UJournalComponent>())
    {
        Journal->AddEntry(
            TAG_Journal_Entry_Loot_GoldenAnchor,  // unique entry tag
            TAG_Journal_Track_Adventure,
            false);  // non-repeatable: only record first discovery
    }
}
```

---

## 7 — UI: Open Journal Tab and Load First Page

```cpp
// In your journal UI widget (owning client only):
void UJournalWidget::OpenTab(FGameplayTag TrackTag)
{
    CurrentTrack      = TrackTag;
    CurrentCollection = FGameplayTag::EmptyTag;
    CurrentPage       = 0;
    RefreshPage();
}

void UJournalWidget::RefreshPage()
{
    TArray<FJournalEntryHandle> Handles = JournalComponent->GetPage(
        CurrentTrack,
        CurrentCollection,
        CurrentPage,
        PageSize);

    int32 TotalCount = JournalComponent->GetFilteredCount(CurrentTrack, CurrentCollection);
    int32 TotalPages = FMath::CeilToInt((float)TotalCount / PageSize);

    PopulateListWithHandles(Handles, TotalPages);
}
```

---

## 8 — UI: Render One Entry's Content

```cpp
void UJournalEntryWidget::ShowEntry(FJournalEntryHandle Handle)
{
    // Show loading state immediately — async path may fire next frame.
    SetLoadingState();

    auto* Registry = GetGameInstance()->GetSubsystem<UJournalRegistrySubsystem>();
    TSoftObjectPtr<UJournalEntryDataAsset> AssetRef = Registry->GetEntryAsset(Handle.EntryTag);

    // Asset is likely already in memory (loaded at registry init) — callback fires immediately.
    UAssetManager::Get().GetStreamableManager().RequestAsyncLoad(
        AssetRef.ToSoftObjectPath(),
        [this, AssetRef]()
        {
            UJournalEntryDataAsset* Asset = AssetRef.Get();
            if (!Asset) return;

            // Guard: widget may have been destroyed before async load completed.
            TWeakObjectPtr<UJournalEntryWidget> WeakThis(this);
            Asset->BuildDetails([WeakThis](FJournalRenderedDetails Details)
            {
                if (!WeakThis.IsValid()) return;
                WeakThis->RichTextBlock->SetText(Details.RichBodyText);
                if (!Details.HeaderImage.IsNull())
                    WeakThis->HeaderImage->SetBrushFromSoftTexture(Details.HeaderImage);
                WeakThis->ClearLoadingState();
            });
        });
}
```

> **Always guard `BuildDetails` callbacks with `TWeakObjectPtr`.** The async load may complete after the widget is destroyed.

---

## 9 — UI: Display Collection Progress

```cpp
void UCollectionProgressWidget::Refresh(FGameplayTag CollectionTag)
{
    auto* Registry = GetGameInstance()->GetSubsystem<UJournalRegistrySubsystem>();
    auto* Journal  = OwningPlayerState->FindComponentByClass<UJournalComponent>();
    if (!Registry || !Journal) return;

    FJournalCollectionProgress Progress =
        Registry->GetCollectionProgress(CollectionTag, Journal->GetClientAcquiredSet());

    ProgressBar->SetPercent(Progress.Ratio());
    CountLabel->SetText(FText::Format(
        LOCTEXT("CollectionCount", "{0} / {1}"),
        Progress.Found, Progress.Total));
}
```

---

## 10 — UI: Listen for New Entries (Notification Toast)

```cpp
// In your HUD or notification manager (owning client only):
void UGameHUD::BindJournalListeners()
{
    UJournalComponent* Journal = PlayerState->FindComponentByClass<UJournalComponent>();
    if (!Journal) return;

    Journal->OnJournalSynced.AddDynamic(this, &UGameHUD::HandleJournalSynced);
    Journal->OnEntryAdded.AddDynamic(this, &UGameHUD::HandleJournalEntryAdded);
}

void UGameHUD::HandleJournalEntryAdded(FJournalEntryHandle NewHandle)
{
    // Load the entry asset to get the title (already in memory from registry init).
    auto* Registry = GetGameInstance()->GetSubsystem<UJournalRegistrySubsystem>();
    if (!Registry) return;

    TSoftObjectPtr<UJournalEntryDataAsset> AssetRef = Registry->GetEntryAsset(NewHandle.EntryTag);
    if (UJournalEntryDataAsset* Asset = AssetRef.Get())
    {
        ShowToast(Asset->GetEntryTitle());
    }
}
```

---

## 11 — Checking Entry Acquisition

```cpp
// Server-side: O(1) authoritative check
bool bAlreadyRead = JournalComponent->HasEntry(TAG_Journal_Entry_Book_PageOfShadows);

// Client-side: O(1) via ClientAcquiredSet (built on login sync)
bool bAlreadyRead = JournalComponent->Client_HasEntry(TAG_Journal_Entry_Book_PageOfShadows);
```
