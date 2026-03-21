# Journal System — Usage Guide

---

## Minimum Viable Integration Checklist

```
☐ GameCore plugin enabled in .uproject
☐ UJournalComponent added to APlayerState constructor
☐ UPersistenceRegistrationComponent already present on APlayerState (shared)
☐ JournalEntry and JournalCollection primary asset types registered in DefaultGame.ini
☐ Gameplay tags copied to DefaultGameplayTags.ini (see Architecture.md)
☐ At least one UJournalEntryDataAsset subclass created in game module
☐ Entry assets placed in Asset Manager scanned directory (/Game/Journal/Entries)
☐ UJournalCollectionDefinition assets created (/Game/Journal/Collections)
☐ Bridge component (or equivalent) subscribes to relevant events and calls AddEntry
☐ UI binds to OnJournalSynced and OnEntryAdded delegates
```

---

## 1. Setup — APlayerState

```cpp
// YourPlayerState.h
UPROPERTY()
TObjectPtr<UJournalComponent> JournalComponent;

// YourPlayerState.cpp — constructor
JournalComponent = CreateDefaultSubobject<UJournalComponent>(TEXT("JournalComponent"));
// UPersistenceRegistrationComponent is already present from other systems.
// JournalComponent auto-registers via GetPersistenceKey() on BeginPlay.
```

---

## 2. Asset Manager Configuration (DefaultGame.ini)

```ini
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

## 3. Create Entry Asset Subclasses (Game Module)

Subclass `UJournalEntryDataAsset` for each content type. Lives in the game module, **not** in GameCore.

```cpp
// BookJournalEntryDataAsset.h  (game module)
UCLASS(BlueprintType)
class UBookJournalEntryDataAsset : public UJournalEntryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Book")
    FText Title;

    UPROPERTY(EditDefaultsOnly, Category="Book", meta=(MultiLine=true))
    FText RichText;  // Authored with UE Rich Text markup

    UPROPERTY(EditDefaultsOnly, Category="Book")
    TSoftObjectPtr<UTexture2D> CoverImage;

    // IJournalEntry
    virtual FText GetEntryTitle_Implementation() const override { return Title; }
    virtual FGameplayTag GetTrackTag_Implementation() const override { return TrackTag; }
    virtual void BuildDetails(TFunction<void(FJournalRenderedDetails)> OnReady) const override
    {
        // Book content is fully inline — no async load required.
        FJournalRenderedDetails Out;
        Out.RichBodyText = RichText;
        Out.HeaderImage  = CoverImage;
        OnReady(Out);
    }
};
```

### Quest Entry with Async Load

```cpp
// QuestJournalEntryDataAsset.h  (game module)
UCLASS(BlueprintType)
class UQuestJournalEntryDataAsset : public UJournalEntryDataAsset
{
    GENERATED_BODY()
public:
    // Soft ref — loaded async on demand only.
    UPROPERTY(EditDefaultsOnly, Category="Quest")
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

## 4. Create Collection Assets (Game Module)

In the Content Browser:
```
/Game/Journal/Collections/
  DA_Collection_BooksOfDarkness
    CollectionTag = Journal.Collection.BooksOfDarkness
    TrackTag      = Journal.Track.Books
    Members       = [Page1, Page2, Page3, Page4]

  DA_Collection_AllLore
    CollectionTag   = Journal.Collection.AllLore
    TrackTag        = Journal.Track.Books
    Members         = [StandaloneEntry]
    SubCollections  = [DA_Collection_BooksOfDarkness, DA_Collection_ScrollsOfFire]
```

---

## 5. Wire the Bridge (Game Module)

Create a bridge component on `APlayerState` that subscribes to `UGameCoreEventBus` events and calls `AddEntry`:

```cpp
// UJournalTrackerBridge : public UActorComponent  (game module)
void UJournalTrackerBridge::BeginPlay()
{
    Super::BeginPlay();
    if (!GetOwner()->HasAuthority()) return;

    auto* Bus     = UGameCoreEventBus::Get(this);
    auto* Journal = GetOwner()->FindComponentByClass<UJournalComponent>();
    if (!Bus || !Journal) return;

    // Quest completed → journal entry
    QuestHandle = Bus->StartListening<FQuestCompletedMessage>(
        TAG_GameCoreEvent_Quest_Completed,
        [Journal](FGameplayTag, const FQuestCompletedMessage& Msg)
        {
            Journal->AddEntry(
                Msg.QuestCompletedTag,
                TAG_Journal_Track_Adventure,
                /*bAllowDuplicates=*/ true);  // repeatable quests allowed
        });

    // Place discovered → non-repeating entry
    PlaceHandle = Bus->StartListening<FPlaceDiscoveredMessage>(
        TAG_GameCoreEvent_World_PlaceDiscovered,
        [Journal](FGameplayTag, const FPlaceDiscoveredMessage& Msg)
        {
            Journal->AddEntry(
                Msg.PlaceTag,
                TAG_Journal_Track_Adventure,
                /*bAllowDuplicates=*/ false);
        });
}

void UJournalTrackerBridge::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* Bus = UGameCoreEventBus::Get(this))
    {
        Bus->StopListening(QuestHandle);
        Bus->StopListening(PlaceHandle);
    }
    Super::EndPlay(Reason);
}
```

---

## 6. Server — Direct AddEntry Call

For systems that don't use the event bus bridge pattern:

```cpp
// Server only — e.g. from an interaction handler
void AMyInteractable::OnBookRead(APlayerState* PlayerState)
{
    if (UJournalComponent* Journal = PlayerState->FindComponentByClass<UJournalComponent>())
    {
        // Books are non-repeating
        Journal->AddEntry(
            TAG_Journal_Entry_BookOfShadowsPage1,
            TAG_Journal_Track_Books,
            /*bAllowDuplicates=*/ false);
    }
}
```

---

## 7. UI — Open Journal Tab and Paginate

```cpp
// In your journal UI widget (client only)
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
        /*PageSize=*/ 20);

    int32 Total      = JournalComponent->GetFilteredCount(CurrentTrack, CurrentCollection);
    int32 TotalPages = FMath::DivideAndRoundUp(Total, 20);

    PopulateListWithHandles(Handles, TotalPages);
}
```

---

## 8. UI — Render One Entry's Content

```cpp
void UJournalEntryWidget::ShowEntry(const FJournalEntryHandle& Handle)
{
    SetLoadingState();

    auto* Registry = GetGameInstance()->GetSubsystem<UJournalRegistrySubsystem>();
    if (!Registry) return;

    TSoftObjectPtr<UJournalEntryDataAsset> AssetRef = Registry->GetEntryAsset(Handle.EntryTag);
    if (AssetRef.IsNull()) return;

    // Entry asset is likely already in memory (loaded sync at subsystem init).
    // RequestAsyncLoad is safe even if already loaded — callback fires immediately.
    UAssetManager::Get().GetStreamableManager().RequestAsyncLoad(
        AssetRef.ToSoftObjectPath(),
        [this, AssetRef]()
        {
            UJournalEntryDataAsset* Asset = AssetRef.Get();
            if (!Asset) return;

            // BuildDetails loads any referenced heavy assets (textures, quest defs)
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

---

## 9. UI — Collection Progress Bar

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
        FText::AsNumber(Progress.Found),
        FText::AsNumber(Progress.Total)));
}
```

---

## 10. UI — Listen for New Entries (Notification Toast)

```cpp
// In your HUD or notification manager — bind after login sync
void UGameHUD::BindJournalListeners()
{
    if (UJournalComponent* Journal = PlayerState->FindComponentByClass<UJournalComponent>())
    {
        Journal->OnJournalSynced.AddDynamic(this, &UGameHUD::HandleJournalSynced);
        Journal->OnEntryAdded.AddDynamic(this, &UGameHUD::HandleJournalEntryAdded);
    }
}

void UGameHUD::HandleJournalEntryAdded(FJournalEntryHandle NewHandle)
{
    auto* Registry = GetGameInstance()->GetSubsystem<UJournalRegistrySubsystem>();
    if (!Registry) return;

    // Entry asset is already in memory — Get() is safe.
    if (UJournalEntryDataAsset* Asset = Registry->GetEntryAsset(NewHandle.EntryTag).Get())
        ShowToast(Asset->GetEntryTitle());
}
```

---

## 11. Subscribing to the Server-Side Event Bus Event

For systems that react to journal entries on the **server** (e.g. achievement system):

```cpp
// Server-side — e.g. in an achievement subsystem
void UAchievementSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    if (auto* Bus = UGameCoreEventBus::Get(this))
    {
        JournalHandle = Bus->StartListening<FJournalEntryAddedMessage>(
            TAG_GameCoreEvent_Journal_EntryAdded,
            [this](FGameplayTag, const FJournalEntryAddedMessage& Msg)
            {
                CheckCollectionCompletion(Msg.PlayerState, Msg.Handle);
            });
    }
}
```
