# Integration Guide

**Sub-page of:** [Journal System](../Journal%20System.md)

---

## Required Setup

### 1. APlayerState

```cpp
// In AYourPlayerState constructor:
JournalComponent = CreateDefaultSubobject<UJournalComponent>(TEXT("JournalComponent"));

// PersistenceRegistrationComponent is shared with other persistable components.
// JournalComponent registers itself via GetPersistenceKey().
// No additional setup needed if UPersistenceRegistrationComponent is already present.
```

### 2. Asset Manager Configuration

Add to `DefaultGame.ini` — see [UJournalRegistrySubsystem](UJournalRegistrySubsystem.md) for the full `ini` block.

### 3. Create Entry Asset Subclasses (Game Module)

Subclass `UJournalEntryDataAsset` for each entry type your game needs:

```
/Game/Journal/Entries/
  BP_BookEntry_BookOfShadowsPage1    (UBookJournalEntryDataAsset)
  BP_BookEntry_BookOfShadowsPage2
  BP_QuestEntry_TreasureHunt         (UQuestJournalEntryDataAsset)
  ...
```

Each asset sets:
- `EntryTag` — unique `Journal.Entry.*` tag
- `TrackTag` — `Journal.Track.Books` or `Journal.Track.Adventure`
- Content fields (title, rich text, soft texture refs, etc.)

### 4. Create Collection Assets (Game Module)

```
/Game/Journal/Collections/
  JC_BooksOfDarkness     CollectionTag=Journal.Collection.BooksOfDarkness
    Members: [Page1, Page2, Page3, Page4]
  JC_AllLore             CollectionTag=Journal.Collection.AllLore
    SubCollections: [JC_BooksOfDarkness, JC_ScrollsOfFire]
```

### 5. Wire the Bridge (Game Module)

Create a bridge component on `APlayerState` that subscribes to GMS events and calls `AddEntry`:

```cpp
// UJournalTrackerBridge : public UActorComponent
// Added to APlayerState alongside UJournalComponent.

void UJournalTrackerBridge::BeginPlay()
{
    Super::BeginPlay();
    if (!GetOwner()->HasAuthority()) return;

    auto* GMS = GetWorld()->GetSubsystem<UGameCoreEventSubsystem>();
    auto* Journal = GetOwner()->FindComponentByClass<UJournalComponent>();
    if (!GMS || !Journal) return;

    // Quest completed → journal entry
    GMS->Subscribe<FQuestCompletedMessage>(
        TAG_GameCoreEvent_Quest_Completed, this,
        [Journal](const FQuestCompletedMessage& Msg)
        {
            // QuestCompletedTag maps 1:1 to a UQuestJournalEntryDataAsset EntryTag.
            Journal->AddEntry(
                Msg.QuestCompletedTag,
                TAG_Journal_Track_Adventure,
                true); // repeatable quests allowed
        });

    // Place discovered → journal entry
    GMS->Subscribe<FPlaceDiscoveredMessage>(
        TAG_GameCoreEvent_World_PlaceDiscovered, this,
        [Journal](const FPlaceDiscoveredMessage& Msg)
        {
            Journal->AddEntry(
                Msg.PlaceTag,
                TAG_Journal_Track_Adventure,
                false); // places are non-repeatable
        });
}
```

---

## UI Usage Samples

### Open Journal Tab — Load First Page

```cpp
// In your journal UI widget (client only):
void UJournalWidget::OpenTab(FGameplayTag TrackTag)
{
    CurrentTrack      = TrackTag;
    CurrentCollection = FGameplayTag::EmptyTag; // no collection filter
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

    int32 TotalCount = JournalComponent->GetFilteredCount(
        CurrentTrack, CurrentCollection);
    int32 TotalPages = FMath::CeilToInt((float)TotalCount / PageSize);

    PopulateListWithHandles(Handles, TotalPages);
}
```

### Render One Entry's Content

```cpp
void UJournalEntryWidget::ShowEntry(FJournalEntryHandle Handle)
{
    // Show loading state immediately.
    SetLoadingState();

    auto* Registry = GetGameInstance()->GetSubsystem<UJournalRegistrySubsystem>();
    TSoftObjectPtr<UJournalEntryDataAsset> AssetRef =
        Registry->GetEntryAsset(Handle.EntryTag);

    // Load the entry asset (likely already in memory from Initialize()).
    UAssetManager::Get().GetStreamableManager().RequestAsyncLoad(
        AssetRef.ToSoftObjectPath(),
        [this, AssetRef]()
        {
            UJournalEntryDataAsset* Asset = AssetRef.Get();
            if (!Asset) return;

            // Async-load content (textures, referenced quest definitions, etc.)
            Asset->BuildDetails([this](FJournalRenderedDetails Details)
            {
                RichTextBlock->SetText(Details.RichBodyText);
                if (!Details.HeaderImage.IsNull())
                    HeaderImage->SetBrushFromSoftTexture(Details.HeaderImage);
                ClearLoadingState();
            });
        });
}
```

### Display Collection Progress

```cpp
void UCollectionProgressWidget::Refresh(FGameplayTag CollectionTag)
{
    auto* Registry   = GetGameInstance()->GetSubsystem<UJournalRegistrySubsystem>();
    auto* Journal    = OwningPlayerState->FindComponentByClass<UJournalComponent>();

    // Client_HasEntry uses ClientAcquiredSet built on login sync.
    // Pass it as the acquired set for progress computation.
    // Build the acquired set from the component's local data:
    const TSet<FGameplayTag>& Acquired = Journal->GetClientAcquiredSet();

    FJournalCollectionProgress Progress =
        Registry->GetCollectionProgress(CollectionTag, Acquired);

    ProgressBar->SetPercent(Progress.Ratio());
    CountLabel->SetText(FText::Format(
        LOCTEXT("CollectionCount", "{0} / {1}"),
        Progress.Found, Progress.Total));
}
```

> **`GetClientAcquiredSet()`** should be exposed as a `const TSet<FGameplayTag>&` accessor on `UJournalComponent`. It returns `ClientAcquiredSet` — the set rebuilt on login sync from the local `Entries` array.

### Listen for New Entries (UI Notification)

```cpp
// In your HUD or notification manager:
void UGameHUD::BindJournalListeners()
{
    UJournalComponent* Journal =
        PlayerState->FindComponentByClass<UJournalComponent>();

    Journal->OnEntryAdded.AddDynamic(
        this, &UGameHUD::HandleJournalEntryAdded);
}

void UGameHUD::HandleJournalEntryAdded(FJournalEntryHandle NewHandle)
{
    // Show a "New Journal Entry" toast with the entry title.
    // Load the entry asset to get the title (already in memory from Initialize()).
    auto* Registry = GetGameInstance()->GetSubsystem<UJournalRegistrySubsystem>();
    auto AssetRef  = Registry->GetEntryAsset(NewHandle.EntryTag);

    if (UJournalEntryDataAsset* Asset = AssetRef.Get())
        ShowToast(Asset->GetEntryTitle());
}
```

---

## Checklist: Minimum Viable Integration

```
☐ GameCore plugin enabled
☐ UJournalComponent added to APlayerState
☐ UPersistenceRegistrationComponent present on APlayerState
☐ JournalEntry and JournalCollection primary asset types registered in DefaultGame.ini
☐ Gameplay tags from File Structure copied to DefaultGameplayTags.ini
☐ At least one UJournalEntryDataAsset subclass created in game module
☐ UJournalEntryDataAsset subclasses placed in scanned directory (/Game/Journal/Entries)
☐ UJournalCollectionDefinition assets created (/Game/Journal/Collections)
☐ UJournalTrackerBridge (or equivalent) added to APlayerState
☐ Bridge subscribes to relevant GMS events and calls AddEntry
☐ UI binds to OnJournalSynced and OnEntryAdded delegates
```
