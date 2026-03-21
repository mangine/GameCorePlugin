# Notification System — Usage

---

## Setup (One-Time)

### 1. Create the Category Config Asset

Create a `UNotificationCategoryConfig` data asset. Add rules for each category tag:

| CategoryTag | MaxStackCount | StackedTitleFormat | bAutoViewOnStack |
|---|---|---|---|
| `Notification.Category.Progression` | 5 | `{Count} progression updates` | false |
| `Notification.Category.Quest` | 10 | `{Count} quest updates` | false |
| `Notification.Category.Combat` | 3 | _(empty — show latest title)_ | true |
| `Notification.Category.System` | 0 (unlimited) | | false |

### 2. Create Channel Bindings and the Channel Config Asset

For each Event Bus channel you want to convert into a notification:

1. Create a subclass of `UNotificationChannelBinding` (C++ or Blueprint).
2. Set `Channel` to the Event Bus tag (e.g. `GameCoreEvent.Quest.Started`).
3. Implement `BuildEntry` to populate the `FNotificationEntry`.

Then create a `UNotificationChannelConfig` data asset and add your binding instances inline.

### 3. Assign in Project Settings

Open **Project Settings → Game → Notification System** and assign:
- `ChannelConfig` → your `UNotificationChannelConfig` asset
- `CategoryConfig` → your `UNotificationCategoryConfig` asset

The subsystem reads these at world initialization for every local player.

---

## Binding UI to Delegates

In your HUD or notification widget's `NativeConstruct`:

```cpp
void UMyNotificationWidget::NativeConstruct()
{
    Super::NativeConstruct();

    ULocalPlayer* LP = GetOwningLocalPlayer();
    if (!LP) return;

    UGameCoreNotificationSubsystem* NS =
        LP->GetSubsystem<UGameCoreNotificationSubsystem>();
    if (!NS) return;

    NS->OnNotificationAdded.AddDynamic(this,   &UMyNotificationWidget::HandleNotificationAdded);
    NS->OnGroupChanged.AddDynamic(this,        &UMyNotificationWidget::HandleGroupChanged);
    NS->OnAllViewed.AddDynamic(this,           &UMyNotificationWidget::HandleAllViewed);
    NS->OnNotificationExpired.AddDynamic(this, &UMyNotificationWidget::HandleExpired);
}

void UMyNotificationWidget::NativeDestruct()
{
    ULocalPlayer* LP = GetOwningLocalPlayer();
    if (UGameCoreNotificationSubsystem* NS =
            LP ? LP->GetSubsystem<UGameCoreNotificationSubsystem>() : nullptr)
    {
        NS->OnNotificationAdded.RemoveDynamic(this,   &UMyNotificationWidget::HandleNotificationAdded);
        NS->OnGroupChanged.RemoveDynamic(this,        &UMyNotificationWidget::HandleGroupChanged);
        NS->OnAllViewed.RemoveDynamic(this,           &UMyNotificationWidget::HandleAllViewed);
        NS->OnNotificationExpired.RemoveDynamic(this, &UMyNotificationWidget::HandleExpired);
    }
    Super::NativeDestruct();
}
```

> **Always unbind in `NativeDestruct`.** The subsystem outlives individual widgets.

---

## Querying State on Widget Open

When a notification panel opens, reflect current state without waiting for a new event:

```cpp
void UNotificationPanelWidget::RefreshFromCurrentState()
{
    UGameCoreNotificationSubsystem* NS = /* ... */;

    UpdateBadgeCount(NS->GetTotalUnviewedCount());

    for (const FNotificationGroup& Group : NS->GetAllGroups())
        AddOrUpdateGroupRow(Group);
}
```

---

## Common Patterns

### Toast Notification (short-lived, auto-dismiss)

```cpp
// In a C++ binding's BuildEntry:
Entry.ExpirySeconds = 5.f;   // Subsystem fires OnNotificationExpired after 5 seconds.
// UI: on OnNotificationAdded, spawn toast widget. On OnNotificationExpired, hide it.
```

### Persistent Notification (user must dismiss)

```cpp
Entry.ExpirySeconds = 0.f;   // Never expires.
// UI: user clicks dismiss → call NS->DismissNotification(Entry.Id).
```

### Combat Alert Stack (auto-view previous, keep only latest 3)

Configure in `UNotificationCategoryConfig`:
- `MaxStackCount = 3`, `bAutoViewOnStack = true`

Result: only the newest 3 alerts stay in the group; adding a new one marks all prior ones viewed automatically.

### Unread Badge on Tab

```cpp
void UQuestTabButton::HandleGroupChanged(const FNotificationGroup& Group)
{
    if (Group.CategoryTag != FGameplayTag::RequestGameplayTag("Notification.Category.Quest")) return;

    BadgeCountText->SetText(FText::AsNumber(Group.UnviewedCount));
    BadgeWidget->SetVisibility(
        Group.UnviewedCount > 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}
```

### Direct Push (no Event Bus event)

```cpp
// Game code pushing a tutorial notification without an Event Bus source.
void UMyTutorialSystem::NotifyPlayerStepCompleted(FText StepTitle)
{
    ULocalPlayer* LP = GetLocalPlayer();
    UGameCoreNotificationSubsystem* NS =
        LP ? LP->GetSubsystem<UGameCoreNotificationSubsystem>() : nullptr;
    if (!NS) return;

    FNotificationEntry Entry;
    Entry.CategoryTag   = FGameplayTag::RequestGameplayTag("Notification.Category.Tutorial");
    Entry.Title         = StepTitle;
    Entry.Body          = LOCTEXT("TutorialBody", "Tutorial step completed.");
    Entry.ExpirySeconds = 8.f;
    NS->PushNotification(Entry);
}
```

---

## Writing a C++ Channel Binding (Typed Payload)

```cpp
// In game module — NOT in GameCore.
UCLASS()
class ULevelUpNotificationBinding : public UNotificationChannelBinding
{
    GENERATED_BODY()
public:
    virtual FNotificationEntry BuildEntry_Implementation(const FGameplayTag& InChannel) const override
    {
        FNotificationEntry Entry;
        Entry.CategoryTag   = FGameplayTag::RequestGameplayTag("Notification.Category.Progression");
        Entry.Title         = FText::Format(
            LOCTEXT("LevelUp", "Level {0}!"), FText::AsNumber(LastMessage.NewLevel));
        Entry.Body          = LOCTEXT("LevelUpBody", "You have levelled up.");
        Entry.ExpirySeconds = 5.f;
        Entry.Metadata.Add("NewLevel", FString::FromInt(LastMessage.NewLevel));
        return Entry;
    }

    virtual void RegisterListener(
        UGameCoreEventSubsystem* Bus,
        UGameCoreNotificationSubsystem* Subsystem,
        TArray<FGameplayMessageListenerHandle>& OutHandles) override
    {
        if (!Bus) return;
        FGameplayMessageListenerHandle Handle =
            Bus->StartListening<FProgressionLevelUpMessage>(
                Channel,
                EGameCoreEventScope::ClientOnly,
                this,
                [this, Subsystem](FGameplayTag InChannel, const FProgressionLevelUpMessage& Msg)
                {
                    LastMessage = Msg;
                    FNotificationEntry Entry = BuildEntry(InChannel);
                    if (!Entry.CategoryTag.IsValid()) return;
                    Subsystem->HandleIncomingEntry(MoveTemp(Entry));
                });
        OutHandles.Add(Handle);
    }

    mutable FProgressionLevelUpMessage LastMessage;
};
```

---

## Writing a Blueprint Channel Binding

Blueprint bindings cannot receive a typed struct directly. Use the Event Bus event as a trigger and pull data from already-replicated game state inside `BuildEntry`.

1. Create a Blueprint subclass of `UNotificationChannelBinding`.
2. Set `Channel` to the Event Bus tag (e.g. `GameCoreEvent.Quest.Started`).
3. Override `BuildEntry` (Blueprint event):
   - Get the local `APlayerController`.
   - Read quest data from its replicated `UQuestComponent`.
   - Fill and return an `FNotificationEntry`.
4. Return a default entry with **invalid `CategoryTag`** to suppress the notification (e.g. if quest data isn't ready yet).

> **Suppression contract**: `HandleIncomingEntry` checks `Entry.CategoryTag.IsValid()` before pushing. An invalid category tag = silent suppress.

---

## Gameplay Tags to Add

Add to `DefaultGameplayTags.ini` in your game module (not in GameCore):

```ini
+GameplayTagList=(Tag="Notification.Category.Progression",  DevComment="Level up, XP milestones")
+GameplayTagList=(Tag="Notification.Category.Quest",         DevComment="Quest started, completed, failed")
+GameplayTagList=(Tag="Notification.Category.Combat",        DevComment="Kill streaks, defeat alerts")
+GameplayTagList=(Tag="Notification.Category.System",        DevComment="Server messages, maintenance notices")
+GameplayTagList=(Tag="Notification.Category.Tutorial",      DevComment="Tutorial step completions")
```

GameCore does not declare any `Notification.*` tags — category taxonomy is always game-specific.
