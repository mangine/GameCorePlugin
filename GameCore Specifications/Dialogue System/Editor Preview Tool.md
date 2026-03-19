# Editor Preview Tool

**Sub-page of:** [Dialogue System](Dialogue%20System%20Overview.md)  
**Module:** `GameCoreEditor` (editor-only, gated by `WITH_EDITOR`)  
**Files:**
- `Editor/Dialogue/DialogueAssetTypeActions.h / .cpp`
- `Editor/Dialogue/SDialoguePreviewWidget.h / .cpp`
- `Editor/Dialogue/DialoguePreviewContext.h / .cpp`

---

## Overview

The editor preview tool opens automatically when a developer double-clicks a `UDialogueAsset` in the Content Browser. It drives `FDialogueSimulator` directly against a locally owned `FDialogueSession` — no PIE session, no world, no network. The entire dialogue can be walked through interactively without leaving the editor.

---

## FAssetTypeActions_DialogueAsset

Registers `UDialogueAsset` as a recognized asset class and overrides `OpenAssetEditor` to open the preview tab.

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

// Implementation: for each UDialogueAsset in InObjects, create a
// FDialogueAssetEditorToolkit and call InitDialogueEditor(Asset).
// The toolkit registers a tab spawner that creates SDialoguePreviewWidget.
```

Register in `GameCoreEditorModule::StartupModule`:

```cpp
void FGameCoreEditorModule::StartupModule()
{
    IAssetTools& AssetTools =
        FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    AssetTools.RegisterAssetTypeActions(
        MakeShareable(new FAssetTypeActions_DialogueAsset()));
}
```

---

## FDialoguePreviewContext

Owns the simulated `FDialogueSession` and the designer-controlled context overrides. The only state the preview widget mutates.

```cpp
// File: Editor/Dialogue/DialoguePreviewContext.h

struct FDialoguePreviewContext
{
    TObjectPtr<UDialogueAsset>   Asset;
    FDialogueSession             Session;
    FDialogueStepResult          LastResult;
    FGameplayTagContainer        ContextTags;      // injected into FRequirementContext.SourceTags
    TMap<FName, FDialogueVariant> ContextVariables; // pre-seeded into Session.Variables
    TArray<FString>              Log;

    void             Restart();
    FDialogueStepResult Step();
    void             SubmitChoice(int32 ChoiceIndex);
    bool             IsFinished() const;

private:
    void AppendStepToLog(const FDialogueStepResult& Result);
};
```

### Restart

```cpp
void FDialoguePreviewContext::Restart()
{
    Session          = FDialogueSession{};
    Session.SessionID = FGuid::NewGuid();
    // Chooser is null — Condition nodes receive context built from ContextTags + ContextVariables.

    FDialogueStackFrame RootFrame;
    RootFrame.Asset            = Asset;
    RootFrame.CurrentNodeIndex = Asset->StartNodeIndex;
    RootFrame.ReturnNodeIndex  = INDEX_NONE;
    Session.AssetStack.Add(RootFrame);
    Session.Variables = ContextVariables;

    Log.Empty();
    Log.Add(FString::Printf(TEXT("[Preview started: %s]"), *Asset->GetName()));

    // Inject ContextTags as a Tag variable so BuildContext picks them up.
    // We store them under a reserved name that Condition nodes can reference.
    // Alternatively, Session.Variables can hold individual Tag variants keyed by FName.
    // The simpler approach: temporarily patch the session's Chooser with a transient
    // UObject that implements ITaggedInterface and returns ContextTags.
    // See note below on the EditorChooserProxy pattern.

    LastResult = Step();
}
```

> **EditorChooserProxy pattern:** Because `FDialogueSimulator::BuildContext` reads tags from `ITaggedInterface` on the Chooser, the editor tool creates a transient `UDialogueEditorChooserProxy : public UObject, public ITaggedInterface` that returns `FDialoguePreviewContext::ContextTags` from `GetOwnedTags()`. This proxy is assigned as `Session.Chooser` during preview. It is never replicated and exists only in the editor module.
>
> ```cpp
> UCLASS(Transient)
> class UDialogueEditorChooserProxy : public UObject, public ITaggedInterface
> {
>     GENERATED_BODY()
> public:
>     FGameplayTagContainer Tags;
>     virtual FGameplayTagContainer GetOwnedTags() const override { return Tags; }
> };
> ```

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

### IsFinished

```cpp
bool FDialoguePreviewContext::IsFinished() const
{
    return LastResult.Action == EDialogueStepAction::EndSession
        || !Session.IsValid();
}
```

### AppendStepToLog

Converts the `FDialogueStepResult` into a human-readable log entry. Must reach into the *current waiting node* to extract speaker/text/choices, because `FDialogueStepResult` does not carry display data — it only carries action routing.

```cpp
void FDialoguePreviewContext::AppendStepToLog(const FDialogueStepResult& Result)
{
    if (!Session.IsValid()) return;

    const FDialogueStackFrame& Frame = Session.CurrentFrame();

    switch (Result.Action)
    {
    case EDialogueStepAction::WaitForACK:
    {
        // CurrentNodeIndex points at the waiting Line node.
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
        // CurrentNodeIndex points at the waiting PlayerChoice node.
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
                Log.Add(FString::Printf(TEXT("  [Timeout: %.1fs → default choice %d]"),
                    ChoiceNode->TimeoutSeconds, ChoiceNode->DefaultChoiceIndex));
        }
        break;
    }

    case EDialogueStepAction::EndSession:
        Log.Add(FString::Printf(TEXT("[SESSION ENDED: %s]"),
            Result.EndReason == EDialogueEndReason::Completed    ? TEXT("Completed")  :
            Result.EndReason == EDialogueEndReason::Interrupted  ? TEXT("Interrupted") :
            Result.EndReason == EDialogueEndReason::AssetError   ? TEXT("AssetError")  :
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
    bool bAutoPlaying = false;
    FTimerHandle AutoPlayTimerHandle;
    void AutoPlayTick();
    void StopAutoPlay();
};
```

### Auto-Play Termination

```cpp
void SDialoguePreviewWidget::AutoPlayTick()
{
    if (Context.IsFinished())
    {
        StopAutoPlay();
        return;
    }

    const FDialogueStepResult& Last = Context.LastResult;

    // Stop at choice nodes — the designer must click a choice manually.
    if (Last.Action == EDialogueStepAction::WaitForChoice)
    {
        StopAutoPlay();
        RebuildChoiceButtons();
        return;
    }

    // Advance ACK-waiting lines automatically.
    if (Last.Action == EDialogueStepAction::WaitForACK)
    {
        // Simulate ACK: advance CurrentNodeIndex to the line's NextNodeIndex.
        const UDialogueNode_Line* LineNode = Cast<UDialogueNode_Line>(
            Context.Session.CurrentFrame().Asset
                ->GetNode(Context.Session.CurrentFrame().CurrentNodeIndex));
        if (LineNode)
        {
            Context.Session.CurrentFrame().CurrentNodeIndex = LineNode->NextNodeIndex;
            Context.Session.bWaiting = false;
        }
        Context.Step();
        ScrollLogToBottom();
    }
}

void SDialoguePreviewWidget::StopAutoPlay()
{
    bAutoPlaying = false;
    if (AutoPlayTimerHandle.IsValid())
    {
        // Editor widgets use GEditor->GetTimerManager() — not UWorld.
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
| SubDialogue push | Light green |
| SubDialogue pop | Light green |
| SetVariable | Light blue |
| Error / AssetError | Red |
| Session start/end | Grey |
| Timeout auto-select | Orange |

---

## Build.cs

```csharp
// GameCoreEditor.Build.cs
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core", "CoreUObject", "Engine",
    "GameCore",
    "UnrealEd",
    "AssetTools",
    "SlateCore", "Slate",
    "GameplayTags",
    "GameplayTagsEditor",
});
```
