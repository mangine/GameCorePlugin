# Usage Guide

**Sub-page of:** [Notification System Overview](Notification%20System%20Overview.md)

This guide covers end-to-end setup from a fresh integration to common UI patterns.

---

## Step 1 — Create the Category Config Asset

1. In the editor, create a new `UNotificationCategoryConfig` data asset.
2. Add rules for each category tag you plan to use:

| CategoryTag | MaxStackCount | StackedTitleFormat | bAutoViewOnStack |
|---|---|---|---|
| `Notification.Category.Progression` | 5 | `{Count} progression updates` | false |
| `Notification.Category.Quest` | 10 | `{Count} quest updates` | false |
| `Notification.Category.Combat` | 3 | _(empty — show latest title)_ | true |
| `Notification.Category.System` | 0 (unlimited) | | false |

---

## Step 2 — Create Channel Bindings and the Channel Config Asset

For each GMS channel you want to turn into a notification:

1. Create a subclass of `UNotificationChannelBinding` (C++ or Blueprint).
2. Set `Channel` to the GMS tag (e.g. `GameCoreEvent.Quest.Started`).
3. Implement `BuildEntry` to populate the `FNotificationEntry`.

Then create a `UNotificationChannelConfig` data asset and add all your binding instances inline.

---

## Step 3 — Assign in Project Settings

Open **Project Settings → Game → Notification System** and assign:
- `ChannelConfig` → your `UNotificationChannelConfig` asset
- `CategoryConfig` → your `UNotificationCategoryConfig` asset

That's all the setup required. The subsystem reads these at world initialization for every local player.

---

## Step 4 — Bind UI to Delegates

In your HUD or UI widget's `NativeConstruct`:

```cpp
void UMyNotificationWidget::NativeConstruct()
{
    Super::NativeConstruct();

    ULocalPlayer* LP = GetOwningLocalPlayer();
    if (!LP) return;

    UGameCoreNotificationSubsystem* NS =
        LP->GetSubsystem<UGameCoreNotificationSubsystem>();
    if (!NS) return;

    NS->OnNotificationAdded.AddDynamic(this, &UMyNotificationWidget::HandleNotificationAdded);
    NS->OnGroupChanged.AddDynamic(this,      &UMyNotificationWidget::HandleGroupChanged);
    NS->OnAllViewed.AddDynamic(this,         &UMyNotificationWidget::HandleAllViewed);
    NS->OnNotificationExpired.AddDynamic(this, &UMyNotificationWidget::HandleExpired);
}

void UMyNotificationWidget::NativeDestruct()
{
    ULocalPlayer* LP = GetOwningLocalPlayer();
    if (UGameCoreNotificationSubsystem* NS = LP ? LP->GetSubsystem<UGameCoreNotificationSubsystem>() : nullptr)
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

## Step 5 — Querying on Widget Init

When a notification panel opens, it should reflect current state without waiting for a new event:

```cpp
void UNotificationPanelWidget::RefreshFromCurrentState()
{
    UGameCoreNotificationSubsystem* NS = /* ... */;

    // Update the total badge.
    UpdateBadgeCount(NS->GetTotalUnviewedCount());

    // Rebuild group list.
    for (const FNotificationGroup& Group : NS->GetAllGroups())
    {
        AddOrUpdateGroupRow(Group);
    }
}
```

---

## Common Patterns

### Toast Notification (short-lived, auto-dismiss)

```cpp
// In a C++ binding's BuildEntry:
Entry.ExpirySeconds = 5.f;   // Subsystem fires OnNotificationExpired after 5s.
// UI: on OnNotificationAdded, spawn toast widget. On OnNotificationExpired, hide it.
```

### Persistent Notification (no expiry, requires user dismiss)

```cpp
Entry.ExpirySeconds = 0.f;   // Never expires.
// UI: user clicks dismiss button → call NS->DismissNotification(Entry.Id).
```

### Combat Alert Stack (auto-view previous, keep only 3)

Configure in `UNotificationCategoryConfig`:
- `MaxStackCount = 3`, `bAutoViewOnStack = true`

Result: only the newest 3 combat alerts stay in the group, and adding a new one marks all prior ones viewed automatically.

### Unread Badge on Tab

```cpp
void UQuestTabButton::OnGroupChanged(const FNotificationGroup& Group)
{
    if (Group.CategoryTag == FGameplayTag::RequestGameplayTag("Notification.Category.Quest"))
    {
        BadgeCountText->SetText(FText::AsNumber(Group.UnviewedCount));
        BadgeWidget->SetVisibility(Group.UnviewedCount > 0
            ? ESlateVisibility::Visible
            : ESlateVisibility::Collapsed);
    }
}
```

### Direct Push (no GMS event)

```cpp
// Game code pushing a tutorial notification without a GMS source.
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

## Restoring Viewed State Across Sessions (Game-Layer Responsibility)

GameCore does not persist viewed state. If the game needs to restore it:

```cpp
// In the game's load flow, after the subsystem is initialized:
void UMyGameSaveSystem::RestoreNotificationState(
    UGameCoreNotificationSubsystem* NS,
    const TArray<FGuid>& PreviouslyViewedIds)
{
    // PreviouslyViewedIds is loaded from the save file.
    // At this point notifications have already been re-pushed (e.g. from quest state).
    for (FGuid ViewedId : PreviouslyViewedIds)
    {
        NS->MarkViewed(ViewedId);
    }
}
```

> Note: Because `FGuid` is ephemeral, this only works if notifications are re-generated deterministically on load (same source events → same category/title). For most toast notifications this pattern is unnecessary — they are transient by design.

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
