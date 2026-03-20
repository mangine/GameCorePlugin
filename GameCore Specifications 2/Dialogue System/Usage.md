# Dialogue System — Usage

---

## Setup Checklist

1. Add `UDialogueComponent` to any actor that can host dialogue (NPC, interactive object, trigger volume).
2. Add `UDialogueManagerComponent` to `APlayerState`.
3. Add GameCore Dialogue gameplay tags to your project's tag ini (see Architecture.md).
4. Create a `UDialogueAsset` in the Content Browser. All `FText` fields **must** use StringTable references.
5. Wire `StartDialogue` from game code to the activation source of your choice.
6. Bind your UI widget to `UDialogueManagerComponent::OnDialogueStateReceived`.

---

## Pattern 1 — Wiring from UInteractionComponent

The dialogue system has no direct knowledge of the Interaction system. Game code bridges them:

```cpp
// In AMyNPC::BeginPlay (server only):
void AMyNPC::BeginPlay()
{
    Super::BeginPlay();
    if (!HasAuthority()) return;

    UInteractionComponent* IC = FindComponentByClass<UInteractionComponent>();
    UDialogueComponent*    DC = FindComponentByClass<UDialogueComponent>();
    if (IC && DC)
    {
        IC->OnInteractionExecuted.AddLambda(
            [DC, this](AActor* Instigator, uint8 EntryIndex)
            {
                if (EntryIndex == 0) // "Talk" entry
                    DC->StartDialogue(Instigator, TalkAsset);
            });
    }
}
```

---

## Pattern 2 — Wiring from a Quest Event

```cpp
// In a quest bridge subsystem (game module):
void UMyQuestBridge::Initialize()
{
    UGameplayMessageSubsystem::Get(this).RegisterListener(
        TAG_GameCoreEvent_Quest_Stage_Started,
        this,
        &UMyQuestBridge::OnQuestStageStarted);
}

void UMyQuestBridge::OnQuestStageStarted(
    FGameplayTag Channel, const FQuestStageMessage& Msg)
{
    if (Msg.StageTag == TAG_Quest_TreasureHunt_Stage_NPCBriefing)
    {
        if (AMyNPC* NPC = FindBriefingNPC())
        {
            if (UDialogueComponent* DC =
                    NPC->FindComponentByClass<UDialogueComponent>())
            {
                DC->StartDialogue(Msg.PlayerActor, BriefingAsset);
            }
        }
    }
}
```

---

## Pattern 3 — Group Dialogue with Custom Chooser Resolution

```cpp
// Before starting, override the Chooser resolution delegate:
void AMyNPC::StartBriefing(const TArray<AActor*>& GroupMembers)
{
    UDialogueComponent* DC = FindComponentByClass<UDialogueComponent>();
    if (!DC) return;

    DC->ResolveChooser.BindLambda(
        [](const TArray<AActor*>& Participants) -> AActor*
        {
            for (AActor* P : Participants)
            {
                if (UGroupComponent* GC =
                        P->FindComponentByClass<UGroupComponent>())
                {
                    if (GC->IsLeader())
                        return P;
                }
            }
            return Participants.IsEmpty() ? nullptr : Participants[0];
        });

    DC->StartGroupDialogue(GroupMembers, BriefingAsset);
}
```

---

## Pattern 4 — Reacting to Dialogue End

```cpp
// Bind once at BeginPlay:
void AMyNPC::BeginPlay()
{
    Super::BeginPlay();
    if (UDialogueComponent* DC = FindComponentByClass<UDialogueComponent>())
        DC->OnDialogueEnded.AddDynamic(this, &AMyNPC::OnDialogueEnded);
}

void AMyNPC::OnDialogueEnded(FGuid SessionID, EDialogueEndReason Reason)
{
    // Resume patrol, trigger walk-away animation, etc.
    if (Reason == EDialogueEndReason::Completed)
        ResumePatrol();
}
```

---

## Pattern 5 — Reacting to Dialogue Events (Game Event Wiring)

`UDialogueNode_Event` fires a GMS message. Your game module listens without the dialogue system knowing those systems exist:

```cpp
// In a quest bridge or listener component:
void UMyDialogueBridge::Initialize()
{
    UGameplayMessageSubsystem::Get(this).RegisterListener(
        TAG_DialogueEvent_Node_Event,
        this,
        &UMyDialogueBridge::OnDialogueEvent);
}

void UMyDialogueBridge::OnDialogueEvent(
    FGameplayTag Channel, const FDialogueEventMessage& Msg)
{
    if (Msg.PayloadTag == TAG_Quest_TreasureHunt_Unlocked)
    {
        if (AActor* Instigator = Msg.Instigator.Get())
        {
            if (APlayerState* PS = Instigator->GetPlayerState<APlayerState>())
            {
                if (UQuestComponent* QC =
                        PS->FindComponentByClass<UQuestComponent>())
                {
                    QC->Server_UnlockQuest(TAG_Quest_TreasureHunt);
                }
            }
        }
    }
}
```

---

## Pattern 6 — UI Widget Binding

```cpp
// In a UUserWidget::NativeConstruct:
void UDialogueWidget::NativeConstruct()
{
    Super::NativeConstruct();

    APlayerState* PS = GetOwningPlayerState();
    if (!PS) return;

    UDialogueManagerComponent* Manager =
        PS->FindComponentByClass<UDialogueManagerComponent>();
    if (!Manager) return;

    Manager->OnDialogueStateReceived.AddDynamic(
        this, &UDialogueWidget::OnStateReceived);
    Manager->OnDialogueSessionEnded.AddDynamic(
        this, &UDialogueWidget::OnSessionEnded);
}

void UDialogueWidget::OnStateReceived(const FDialogueClientState& State)
{
    // Resolve speaker display name and portrait from your game's speaker registry
    // using State.SpeakerTag as the key. The dialogue system only carries the tag.
    SpeakerNameText->SetText(SpeakerRegistry->GetDisplayName(State.SpeakerTag));
    LineText->SetText(State.LineText);

    ChoiceBox->ClearChildren();
    if (State.bWaitingForChoice && !State.bIsObserver)
    {
        for (const FDialogueClientChoice& Choice : State.Choices)
        {
            UDialogueChoiceButton* Btn = CreateWidget<UDialogueChoiceButton>(this, ChoiceButtonClass);
            Btn->Setup(Choice, State.SessionID, Manager);
            Btn->SetIsEnabled(!Choice.bLocked);
            ChoiceBox->AddChild(Btn);
        }
    }

    // Show timeout bar if applicable. Client counts down locally.
    if (State.TimeoutSeconds > 0.0f)
    {
        TimeoutBar->SetVisibility(ESlateVisibility::Visible);
        StartLocalCountdown(State.TimeoutRemainingSeconds, State.TimeoutSeconds);
    }
    else
    {
        TimeoutBar->SetVisibility(ESlateVisibility::Collapsed);
    }

    // Play voice-over locally — async load is preferred.
    if (!State.VoiceCue.IsNull())
    {
        UAssetManager::GetStreamableManager().RequestAsyncLoad(
            State.VoiceCue.ToSoftObjectPath(),
            [this, VoiceCue = State.VoiceCue]()
            {
                if (USoundBase* Sound = VoiceCue.Get())
                    UGameplayStatics::PlaySound2D(this, Sound);
            });
    }

    // Show/hide the ACK button.
    ContinueButton->SetVisibility(
        State.bWaitingForACK
            ? ESlateVisibility::Visible
            : ESlateVisibility::Collapsed);

    SetVisibility(ESlateVisibility::Visible);
}

void UDialogueWidget::OnSessionEnded(FGuid SessionID, EDialogueEndReason Reason)
{
    SetVisibility(ESlateVisibility::Collapsed);
    ClearCountdown();
}

// Local countdown — no server sync, purely cosmetic.
void UDialogueWidget::StartLocalCountdown(float Remaining, float Total)
{
    TotalTimeoutSeconds    = Total;
    CountdownRemaining     = Remaining;

    GetWorld()->GetTimerManager().SetTimer(
        CountdownTimer,
        [this]()
        {
            CountdownRemaining -= 0.1f;
            TimeoutBar->SetPercent(
                FMath::Max(CountdownRemaining / TotalTimeoutSeconds, 0.0f));
            if (CountdownRemaining <= 0.0f)
                ClearCountdown();
        },
        0.1f,
        /*bLoop=*/true);
}

void UDialogueWidget::ClearCountdown()
{
    GetWorld()->GetTimerManager().ClearTimer(CountdownTimer);
    TimeoutBar->SetVisibility(ESlateVisibility::Collapsed);
}
```

---

## Pattern 7 — Choice Button Submitting a Choice

```cpp
// In UDialogueChoiceButton:
void UDialogueChoiceButton::Setup(
    const FDialogueClientChoice& InChoice,
    FGuid InSessionID,
    UDialogueManagerComponent* InManager)
{
    SessionID   = InSessionID;
    ChoiceIndex = InChoice.ChoiceIndex;
    Manager     = InManager;

    LabelText->SetText(InChoice.Label);

    if (InChoice.bLocked)
    {
        LockIcon->SetVisibility(ESlateVisibility::Visible);
        // Optionally set a tooltip from InChoice.LockReasonText.
    }
}

FReply UDialogueChoiceButton::NativeOnMouseButtonDown(
    const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (Manager.IsValid())
        Manager->SubmitChoice(SessionID, ChoiceIndex);
    return FReply::Handled();
}
```

---

## Authoring a Dialogue Asset

1. Right-click in Content Browser → **GameCore → Dialogue Asset**.
2. Double-click the asset to open the Dialogue Preview Tool.
3. Set `SessionMode` to **Single** or **Group** once per asset.
4. Add nodes in the graph area. Connect Start → Line → PlayerChoice → branches.
5. All `FText` fields **must** reference a StringTable entry — raw string literals trigger a validation warning on asset save.
6. Use the **Context Override** panel in the preview tool to simulate requirement condition tags.
7. Press **Play** (Auto-Play) to walk through all non-choice steps automatically.
8. Click choice buttons manually to select branches during preview.

---

## Forcing a Session to End

```cpp
// From game code, e.g. on combat start:
void AMyNPC::OnCombatStarted()
{
    if (UDialogueComponent* DC = FindComponentByClass<UDialogueComponent>())
    {
        // End any active session for the instigator — all participants notified.
        if (DC->HasActiveSession(CombatInstigator))
            DC->ForceEndSession(DC->GetSessionIDForInstigator(CombatInstigator));
    }
}
```

> **Note:** `GetSessionIDForInstigator` is a private method on `UDialogueComponent`. If game code needs to force-end sessions, expose it as `BlueprintAuthorityOnly` or provide a `ForceEndAllSessions()` variant.

---

## Using Session Variables in Dialogue Assets

Session variables are written by `UDialogueNode_SetVariable` and evaluated by `UDialogueNode_Condition` via `FRequirementContext`.

**Example flow:**
1. Author a `SetVariable` node early in the dialogue that sets `VariableName = "WarnedPlayer"`, `Value = true (Bool)`.
2. Author a `Condition` node later that checks via a `URequirement` subclass reading `FRequirementContext::PersistedData["WarnedPlayer"]`.
3. The condition branches to different lines based on whether the warning was given in this session.

Session variables are discarded when the session ends. For persistent "has the player seen this before" checks across sessions, author a game-side `URequirement` subclass that queries a persistent store — this is intentionally outside the dialogue system.
