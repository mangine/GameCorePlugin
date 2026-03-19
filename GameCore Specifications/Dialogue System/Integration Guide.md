# Integration Guide

**Sub-page of:** [Dialogue System](Dialogue%20System%20Overview.md)

---

## Setup Checklist

1. Add `UDialogueComponent` to any actor that can host dialogue (NPC, object, trigger).
2. Add `UDialogueManagerComponent` to `APlayerState`.
3. Add the GameCore Dialogue gameplay tags to your project tag ini (see [GMS Events](GMS%20Events.md)).
4. Create a `UDialogueAsset` in the Content Browser. All `FText` fields must use StringTable references.
5. Wire `StartDialogue` from game code to the activation source of your choice (see patterns below).
6. Bind UI widget to `UDialogueManagerComponent::OnDialogueStateReceived`.

---

## Pattern 1 — Wiring from UInteractionComponent

```cpp
// In AMyNPC::BeginPlay (server only):
if (HasAuthority())
{
    UInteractionComponent* IC = FindComponentByClass<UInteractionComponent>();
    UDialogueComponent*    DC = FindComponentByClass<UDialogueComponent>();
    if (IC && DC)
    {
        IC->OnInteractionExecuted.AddLambda([DC, this](AActor* Instigator, uint8 EntryIndex)
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
UGameplayMessageSubsystem::Get(this).RegisterListener(
    TAG_GameCoreEvent_Quest_Stage_Started,
    this,
    &UMyQuestBridge::OnQuestStageStarted);

void UMyQuestBridge::OnQuestStageStarted(FGameplayTag Channel, const FQuestStageMessage& Msg)
{
    if (Msg.StageTag == TAG_Quest_TreasureHunt_Stage_NPCBriefing)
    {
        AMyNPC* NPC = FindBriefingNPC();
        if (UDialogueComponent* DC = NPC->FindComponentByClass<UDialogueComponent>())
            DC->StartDialogue(Msg.PlayerActor, BriefingAsset);
    }
}
```

---

## Pattern 3 — UI Binding

```cpp
// In a UUserWidget's NativeConstruct:
APlayerState* PS = GetOwningPlayerState();
if (UDialogueManagerComponent* Manager =
        PS->FindComponentByClass<UDialogueManagerComponent>())
{
    Manager->OnDialogueStateReceived.AddDynamic(
        this, &UDialogueWidget::OnStateReceived);
    Manager->OnDialogueSessionEnded.AddDynamic(
        this, &UDialogueWidget::OnSessionEnded);
}

void UDialogueWidget::OnStateReceived(const FDialogueClientState& State)
{
    // Update speaker portrait from SpeakerTag via your speaker registry.
    SpeakerNameText->SetText(SpeakerRegistry->GetDisplayName(State.SpeakerTag));
    LineText->SetText(State.LineText);

    ChoiceBox->ClearChildren();
    for (const FDialogueClientChoice& Choice : State.Choices)
    {
        UDialogueChoiceButton* Btn = CreateWidget<UDialogueChoiceButton>(...);
        Btn->Setup(Choice, State.SessionID, Manager);
        Btn->SetIsEnabled(!Choice.bLocked);
        ChoiceBox->AddChild(Btn);
    }

    // Show timeout bar if applicable.
    TimeoutBar->SetVisibility(
        State.TimeoutSeconds > 0.0f ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

    if (State.TimeoutSeconds > 0.0f)
        StartLocalCountdown(State.TimeoutRemainingSeconds);

    // Play voice-over locally.
    if (!State.VoiceCue.IsNull())
    {
        State.VoiceCue.LoadSynchronous(); // Or async — caller's choice.
        UGameplayStatics::PlaySound2D(this, State.VoiceCue.Get());
    }
}
```

---

## Pattern 4 — Reacting to Dialogue Events (Quest Wiring)

```cpp
// Register once in your quest bridge or a dedicated dialogue listener component:
UGameplayMessageSubsystem::Get(this).RegisterListener(
    TAG_DialogueEvent_Node_Event,
    this,
    &UMyDialogueBridge::OnDialogueEvent);

void UMyDialogueBridge::OnDialogueEvent(
    FGameplayTag Channel, const FDialogueEventMessage& Msg)
{
    // PayloadTag identifies the specific game event.
    if (Msg.PayloadTag == TAG_Quest_TreasureHunt_Unlocked)
    {
        APlayerState* PS = Cast<APlayerState>(
            Msg.Instigator.Get() ? Msg.Instigator->GetPlayerState() : nullptr);
        if (UQuestComponent* QC = PS->FindComponentByClass<UQuestComponent>())
            QC->Server_UnlockQuest(TAG_Quest_TreasureHunt);
    }
}
```

---

## Pattern 5 — Group Dialogue with Custom Chooser

```cpp
// On the NPC, before starting:
DC->ResolveChooser.BindLambda([this](const TArray<AActor*>& Participants) -> AActor*
{
    // Return group leader if present, otherwise first participant.
    for (AActor* P : Participants)
    {
        if (UGroupComponent* GC = P->FindComponentByClass<UGroupComponent>())
            if (GC->IsLeader())
                return P;
    }
    return Participants.IsEmpty() ? nullptr : Participants[0];
});

DC->StartGroupDialogue(GroupMembers, BriefingAsset);
```

---

## Timeout UI — Local Countdown Pattern

```cpp
// In UDialogueWidget:
void UDialogueWidget::StartLocalCountdown(float RemainingSeconds)
{
    CountdownRemaining = RemainingSeconds;
    GetWorld()->GetTimerManager().SetTimer(
        CountdownTimer,
        [this]()
        {
            CountdownRemaining -= 0.1f;
            TimeoutBar->SetPercent(FMath::Max(CountdownRemaining / TotalTimeoutSeconds, 0.0f));
            if (CountdownRemaining <= 0.0f)
                GetWorld()->GetTimerManager().ClearTimer(CountdownTimer);
        },
        0.1f,
        /*bLoop=*/true);
}
```

---

## File Structure

```
GameCore/Source/GameCore/
└── Dialogue/
    ├── DialogueEnums.h
    ├── DialogueTypes.h           // FDialogueVariant, FDialogueStackFrame, FDialogueSession,
    │                             //   FDialogueClientChoice, FDialogueClientState,
    │                             //   FDialogueEventMessage, FDialogueStepResult
    ├── DialogueSimulator.h / .cpp
    ├── Assets/
    │   └── DialogueAsset.h / .cpp
    ├── Nodes/
    │   ├── DialogueNode.h        // Abstract base
    │   ├── DialogueNode_Line.h / .cpp
    │   ├── DialogueNode_PlayerChoice.h / .cpp
    │   ├── DialogueNode_Condition.h / .cpp
    │   ├── DialogueNode_Event.h / .cpp
    │   ├── DialogueNode_SetVariable.h / .cpp
    │   ├── DialogueNode_SubDialogue.h / .cpp
    │   ├── DialogueNode_Jump.h / .cpp
    │   └── DialogueNode_End.h / .cpp
    └── Components/
        ├── DialogueComponent.h / .cpp
        └── DialogueManagerComponent.h / .cpp

GameCore/Source/GameCoreEditor/
└── Dialogue/
    ├── DialogueAssetTypeActions.h / .cpp
    ├── SDialoguePreviewWidget.h / .cpp
    └── DialoguePreviewContext.h / .cpp
```

## Build.cs Dependencies

```csharp
// GameCore.Build.cs — add to existing:
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameplayTags",
    "GameplayMessageSubsystem",
});

// Dialogue has zero dependencies on Interaction, Quest, Progression, or any other
// GameCore feature system. It depends only on: Requirement System (for URequirement_Composite)
// and Event Bus (for GMS broadcast in UDialogueNode_Event).
```
