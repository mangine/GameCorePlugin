# Dialogue System — Editor Preview Tool

**Module:** `GameCoreEditor` (editor-only, gated by `WITH_EDITOR`)  
**Files:**
- `Editor/Dialogue/DialogueAssetTypeActions.h / .cpp`
- `Editor/Dialogue/DialoguePreviewContext.h / .cpp`
- `Editor/Dialogue/SDialoguePreviewWidget.h / .cpp`

---

## Overview

Opens automatically when a developer double-clicks a `UDialogueAsset` in the Content Browser. Drives `FDialogueSimulator` directly on a locally owned `FDialogueSession` — no PIE, no world, no network. The entire dialogue can be walked through interactively without launching the game.

---

## FAssetTypeActions_DialogueAsset

Registers `UDialogueAsset` as a recognized asset class and opens the preview editor tab.

```cpp
// File: Editor/Dialogue/DialogueAssetTypeActions.h

class FAssetTypeActions_DialogueAsset : public FAssetTypeActions_Base
{
public:
    virtual FText    GetName()           const override { return NSLOCTEXT("GameCoreEditor", "DialogueAsset", "Dialogue Asset"); }
    virtual FColor   GetTypeColor()      const override { return FColor(0, 175, 200); }
    virtual UClass*  GetSupportedClass() const override { return UDialogueAsset::StaticClass(); }
    virtual uint32   GetCategories()           override { return EAssetTypeCategories::Gameplay; }

    virtual void OpenAssetEditor(
        const TArray<UObject*>& InObjects,
        TSharedPtr<IToolkitHost> EditWithinLevelEditor) override;
};
// Implementation: for each UDialogueAsset, create FDialogueAssetEditorToolkit
// and call InitDialogueEditor(Asset). The toolkit spawns SDialoguePreviewWidget.
```

Register in `GameCoreEditorModule::StartupModule`:
```cpp
IAssetTools& AssetTools =
    FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
AssetTools.RegisterAssetTypeActions(
    MakeShareable(new FAssetTypeActions_DialogueAsset()));
```

---

## UDialogueEditorChooserProxy

A transient `UObject` that implements `ITaggedInterface` to inject designer-controlled context tags into `FDialogueSimulator::BuildContext`. Assigned as `FDialogueSession::Chooser` during preview. Never replicated.

```cpp
// File: Editor/Dialogue/DialoguePreviewContext.h

UCLASS(Transient)
class UDialogueEditorChooserProxy : public UObject, public ITaggedInterface
{
    GENERATED_BODY()
public:
    FGameplayTagContainer Tags;
    virtual FGameplayTagContainer GetOwnedTags() const override { return Tags; }
};
```

---

## FDialoguePreviewContext

Owns the simulated `FDialogueSession` and the designer-controlled context overrides. The only mutable state the preview widget operates on.

```cpp
// File: Editor/Dialogue/DialoguePreviewContext.h

struct FDialoguePreviewContext
{
    TObjectPtr<UDialogueAsset>    Asset;
    FDialogueSession              Session;
    FDialogueStepResult           LastResult;
    FGameplayTagContainer         ContextTags;       // Injected into FRequirementContext.SourceTags via ChooserProxy
    TMap<FName, FDialogueVariant> ContextVariables;  // Pre-seeded into Session.Variables at Restart
    TArray<FString>               Log;

    void                Restart();
    FDialogueStepResult Step();
    void                SubmitChoice(int32 ChoiceIndex);
    void                SimulateACK();
    bool                IsFinished() const;

private:
    void AppendStepToLog(const FDialogueStepResult& Result);
    TObjectPtr<UDialogueEditorChooserProxy> ChooserProxy;
};
```

### Restart

```cpp
void FDialoguePreviewContext::Restart()
{
    // Create or reset the chooser proxy with current ContextTags.
    if (!ChooserProxy)
        ChooserProxy = NewObject<UDialogueEditorChooserProxy>();
    ChooserProxy->Tags = ContextTags;

    Session           = FDialogueSession{};
    Session.SessionID  = FGuid::NewGuid();
    Session.Chooser    = ChooserProxy; // Non-null chooser for BuildContext to work correctly.
    Session.Participants.Add(ChooserProxy);
    Session.Variables  = ContextVariables;

    FDialogueStackFrame RootFrame;
    RootFrame.Asset            = Asset;
    RootFrame.CurrentNodeIndex = Asset->StartNodeIndex;
    RootFrame.ReturnNodeIndex  = INDEX_NONE;
    Session.AssetStack.Add(RootFrame);

    Log.Empty();
    Log.Add(FString::Printf(TEXT("[Preview started: %s]"), *Asset->GetName()));

    LastResult = Step();
}
```

### Step

```cpp
FDialogueStepResult FDialoguePreviewContext::Step()
{
    if (IsFinished()) return LastResult;
    LastResult = FDialogueSimulator::Advance(Session);
    AppendStepToLog(LastResult);
    return LastResult;
}
```

### SubmitChoice

```cpp
void FDialoguePreviewContext::SubmitChoice(int32 ChoiceIndex)
{
    if (!Session.bWaiting) return;

    const int32 TargetNode = FDialogueSimulator::ResolveChoice(Session, ChoiceIndex);
    if (TargetNode == INDEX_NONE)
    {
        Log.Add(FString::Printf(
            TEXT("[CHOICE REJECTED: index %d is locked or invalid]"), ChoiceIndex));
        return;
    }

    // Log the chosen option label before advancing.
    const UDialogueNode_PlayerChoice* ChoiceNode = Cast<UDialogueNode_PlayerChoice>(
        Session.CurrentFrame().Asset->GetNode(Session.CurrentFrame().CurrentNodeIndex));
    if (ChoiceNode && ChoiceNode->Choices.IsValidIndex(ChoiceIndex))
    {
        Log.Add(FString::Printf(TEXT("[YOU] \u25ba %s"),
            *ChoiceNode->Choices[ChoiceIndex].Label.ToString()));
    }

    Session.CurrentFrame().CurrentNodeIndex = TargetNode;
    Session.bWaiting = false;
    Step();
}
```

### SimulateACK

```cpp
void FDialoguePreviewContext::SimulateACK()
{
    if (!Session.bWaiting) return;

    const UDialogueNode_Line* LineNode = Cast<UDialogueNode_Line>(
        Session.CurrentFrame().Asset->GetNode(Session.CurrentFrame().CurrentNodeIndex));
    if (!LineNode) return;

    Session.CurrentFrame().CurrentNodeIndex = LineNode->NextNodeIndex;
    Session.bWaiting = false;
    Step();
}
```

### IsFinished

```cpp
bool FDialoguePreviewContext::IsFinished() const
{
    return LastResult.Action == EDialogueStepAction::EndSession
        || !Session.IsValid();
}
```

### AppendStepToLog

Converts the current `FDialogueStepResult` into human-readable log entries. Reads display data from the waiting node at `CurrentNodeIndex`.

```cpp
void FDialoguePreviewContext::AppendStepToLog(const FDialogueStepResult& Result)
{
    if (!Session.IsValid()) return;

    const FDialogueStackFrame& Frame = Session.CurrentFrame();

    switch (Result.Action)
    {
    case EDialogueStepAction::WaitForACK:
    {
        const UDialogueNode_Line* LineNode =
            Cast<UDialogueNode_Line>(Frame.Asset->GetNode(Frame.CurrentNodeIndex));
        if (LineNode)
        {
            Log.Add(FString::Printf(TEXT("[%s] %s"),
                *LineNode->SpeakerTag.ToString(),
                *LineNode->LineText.ToString()));
        }
        break;
    }

    case EDialogueStepAction::WaitForChoice:
    {
        const UDialogueNode_PlayerChoice* ChoiceNode =
            Cast<UDialogueNode_PlayerChoice>(Frame.Asset->GetNode(Frame.CurrentNodeIndex));
        if (ChoiceNode)
        {
            Log.Add(TEXT("[Waiting for choice...]"));
            for (int32 i = 0; i < ChoiceNode->Choices.Num(); ++i)
            {
                const FDialogueChoiceConfig& C = ChoiceNode->Choices[i];
                const bool bLocked = C.LockCondition &&
                    !URequirementLibrary::EvaluateRequirement(
                        C.LockCondition,
                        FDialogueSimulator::BuildContext(Session)).bPassed;

                if (bLocked)
                    Log.Add(FString::Printf(TEXT("  [\U0001F512 %d] %s  (%s)"),
                        i, *C.Label.ToString(), *C.LockReasonText.ToString()));
                else
                    Log.Add(FString::Printf(TEXT("  [%d] %s"), i, *C.Label.ToString()));
            }
            if (ChoiceNode->TimeoutSeconds > 0.0f)
                Log.Add(FString::Printf(TEXT("  [Timeout: %.1fs -> default choice %d]"),
                    ChoiceNode->TimeoutSeconds, ChoiceNode->DefaultChoiceIndex));
        }
        break;
    }

    case EDialogueStepAction::EndSession:
        Log.Add(FString::Printf(TEXT("[SESSION ENDED: %s]"),
            Result.EndReason == EDialogueEndReason::Completed    ? TEXT("Completed")          :
            Result.EndReason == EDialogueEndReason::Interrupted  ? TEXT("Interrupted")        :
            Result.EndReason == EDialogueEndReason::AssetError   ? TEXT("AssetError")          :
            TEXT("ChooserDisconnected")));
        break;

    default:
        break;
    }
}
```

---

## SDialoguePreviewWidget

```cpp
// File: Editor/Dialogue/SDialoguePreviewWidget.h

class SDialoguePreviewWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDialoguePreviewWidget) {}
        SLATE_ARGUMENT(TObjectPtr<UDialogueAsset>, DialogueAsset)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    FDialoguePreviewContext Context;

    TSharedPtr<SScrollBox>   LogScrollBox;
    TSharedPtr<SVerticalBox> ChoiceBox;

    FReply OnRestart();
    FReply OnStep();
    FReply OnAutoPlay();
    FReply OnChoiceClicked(int32 ChoiceIndex);

    void RebuildChoiceButtons();
    void AppendLogLine(const FString& Line, FLinearColor Color = FLinearColor::White);
    void ScrollLogToBottom();

    // Auto-play: advances non-choice steps automatically on a timer.
    // Stops on WaitForChoice or IsFinished().
    bool         bAutoPlaying = false;
    FTimerHandle AutoPlayTimerHandle;
    void AutoPlayTick();
    void StopAutoPlay();
};
```

### Auto-Play Implementation

```cpp
void SDialoguePreviewWidget::AutoPlayTick()
{
    if (Context.IsFinished())
    {
        StopAutoPlay();
        return;
    }

    const FDialogueStepResult& Last = Context.LastResult;

    if (Last.Action == EDialogueStepAction::WaitForChoice)
    {
        StopAutoPlay();
        RebuildChoiceButtons();
        return;
    }

    if (Last.Action == EDialogueStepAction::WaitForACK)
    {
        Context.SimulateACK();
        ScrollLogToBottom();
    }
}

void SDialoguePreviewWidget::StopAutoPlay()
{
    bAutoPlaying = false;
    if (AutoPlayTimerHandle.IsValid())
    {
        // Editor widgets use GEditor->GetTimerManager(), not UWorld.
        GEditor->GetTimerManager()->ClearTimer(AutoPlayTimerHandle);
        AutoPlayTimerHandle.Invalidate();
    }
}
```

---

## Log Entry Colours

| Entry type | Colour |
|---|---|
| NPC / speaker line | White |
| Player choice taken | Cyan |
| Condition result | Yellow |
| Event fired | Orange |
| SubDialogue push/pop | Light green |
| SetVariable | Light blue |
| Error / AssetError | Red |
| Session start/end | Grey |
| Timeout auto-select | Orange |
