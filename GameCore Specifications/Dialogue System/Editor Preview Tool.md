# Editor Preview Tool

**Sub-page of:** [Dialogue System](Dialogue%20System%20Overview.md)  
**Module:** `GameCoreEditor` (editor-only module, gated by `WITH_EDITOR`)  
**Files:**
- `Editor/Dialogue/DialogueAssetTypeActions.h / .cpp`
- `Editor/Dialogue/SDialoguePreviewWidget.h / .cpp`
- `Editor/Dialogue/DialoguePreviewContext.h / .cpp`

---

## Overview

The editor preview tool opens automatically when the developer double-clicks a `UDialogueAsset` in the Content Browser. It uses `FDialogueSimulator` directly — no PIE session, no world, no network. The full dialogue can be walked through interactively in the editor.

---

## FAssetTypeActions_DialogueAsset

Registers `UDialogueAsset` as a recognized asset class and overrides `OpenAssetEditor` to open the preview tab.

```cpp
// File: Editor/Dialogue/DialogueAssetTypeActions.h

class FAssetTypeActions_DialogueAsset : public FAssetTypeActions_Base
{
public:
    virtual FText GetName() const override
        { return NSLOCTEXT("GameCoreEditor", "DialogueAsset", "Dialogue Asset"); }

    virtual FColor GetTypeColor() const override { return FColor(0, 175, 200); }

    virtual UClass* GetSupportedClass() const override { return UDialogueAsset::StaticClass(); }

    virtual uint32 GetCategories() override
        { return EAssetTypeCategories::Gameplay; }

    virtual void OpenAssetEditor(
        const TArray<UObject*>& InObjects,
        TSharedPtr<IToolkitHost> EditWithinLevelEditor) override;
};

// Implementation: for each UDialogueAsset in InObjects, create a new
// FDialogueAssetEditorToolkit and call InitDialogueEditor(Asset).
```

Register in `GameCoreEditorModule::StartupModule`:

```cpp
void FGameCoreEditorModule::StartupModule()
{
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    AssetTools.RegisterAssetTypeActions(
        MakeShareable(new FAssetTypeActions_DialogueAsset()));
}
```

---

## FDialoguePreviewContext

Holds the simulated `FDialogueSession` and the designer-controlled context overrides. This is the only state the preview widget mutates.

```cpp
// File: Editor/Dialogue/DialoguePreviewContext.h

struct FDialoguePreviewContext
{
    // The asset currently being previewed.
    TObjectPtr<UDialogueAsset> Asset;

    // The live simulation session. Reset on Restart.
    FDialogueSession Session;

    // Last step result from FDialogueSimulator::Advance.
    FDialogueStepResult LastResult;

    // Designer-supplied Gameplay Tags injected into FRequirementContext
    // when Condition nodes evaluate. Lets the designer test both branches.
    FGameplayTagContainer ContextTags;

    // Designer-supplied named variables pre-seeded into Session.Variables.
    TMap<FName, FDialogueVariant> ContextVariables;

    // Log of all steps taken: each entry is a display string.
    TArray<FString> Log;

    // Initialise/reset the session from Asset.
    void Restart();

    // Advance one step. Calls FDialogueSimulator::Advance and appends to Log.
    FDialogueStepResult Step();

    // Submit a choice. Calls FDialogueSimulator::ResolveChoice then Step.
    void SubmitChoice(int32 ChoiceIndex);

    // Returns true if the session is finished.
    bool IsFinished() const;

private:
    void AppendLineToLog(const FDialogueStepResult& Result);
};
```

### Restart

```cpp
void FDialoguePreviewContext::Restart()
{
    Session = FDialogueSession{};
    Session.SessionID = FGuid::NewGuid();
    // Chooser is null in preview — Condition nodes receive an empty context.
    // Designer sets ContextTags to simulate requirement pass/fail.

    FDialogueStackFrame RootFrame;
    RootFrame.Asset            = Asset;
    RootFrame.CurrentNodeIndex = Asset->StartNodeIndex;
    RootFrame.ReturnNodeIndex  = INDEX_NONE;
    Session.AssetStack.Add(RootFrame);

    // Pre-seed session variables from context overrides.
    Session.Variables = ContextVariables;

    Log.Empty();
    Log.Add(FString::Printf(TEXT("[Preview started: %s]"), *Asset->GetName()));

    LastResult = Step();
}
```

---

## SDialoguePreviewWidget

A Slate widget displayed in the asset editor tab. Owns a `FDialoguePreviewContext` and renders the conversation log, current choices, and context override controls.

### Layout

```
┌─────────────────────────────────────────────────────┐
│  [Restart]  [Auto-Play ▶]  [Step ▶]                  │
├──────────────────────┬──────────────────────────────┤
│  Conversation Log    │  Context Override             │
│  ─────────────────── │  ─────────────────────────── │
│  [NPC] Ahoy, sailor. │  Tags:                        │
│  [NPC] What brings   │  [ Dialogue.Tag.Here    + ]   │
│        ye here?      │                               │
│  [YOU] ► I need      │  Variables:                   │
│         supplies.    │  [ Name ] [ Type ] [ Value ]  │
│  [NPC] Aye, plenty.  │  [         Add Variable      ]│
│  [COND → TRUE]       │                               │
│  [EVENT: Quest.Start]│                               │
│  [→ GenericFarewell] │                               │
│  [← MyNPC_Intro]     │                               │
├──────────────────────┴──────────────────────────────┤
│  Choices (waiting for input):                        │
│  [ 1 ] Tell me about the port.                       │
│  [ 2 ] I'll be on my way.                            │
│  [🔒 3] Buy weapons.  (Req: Level 5 not met)         │
└─────────────────────────────────────────────────────┘
```

### Slate Declaration (abbreviated)

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

    TSharedPtr<SScrollBox>         LogScrollBox;
    TSharedPtr<SVerticalBox>       ChoiceBox;
    TSharedPtr<SGameplayTagWidget> TagOverrideWidget;

    // Button handlers
    FReply OnRestart();
    FReply OnStep();
    FReply OnAutoPlay();
    FReply OnChoiceClicked(int32 ChoiceIndex);

    // Rebuild the choice button list from Context.LastResult.
    void RebuildChoiceButtons();

    // Append a new line to the log scroll box.
    void AppendLog(const FString& Line, FLinearColor Color = FLinearColor::White);

    // Auto-play: repeatedly call Context.Step() on a timer until WaitForChoice or Finished.
    FTimerHandle AutoPlayTimer;
    void AutoPlayTick();
};
```

### Log Entry Colours

| Entry type | Colour |
|---|---|
| NPC line | White |
| Player line (choice taken) | Cyan |
| Condition result | Yellow |
| Event fired | Orange |
| SubDialogue push | Light green |
| SubDialogue pop (return) | Light green |
| Error / AssetError | Red |
| Session start/end | Grey |

### Log String Format per Node Type

```
Line:          [SpeakerTag] Line text here.
Choice taken:  [YOU] ► Choice label text.
Condition:     [CONDITION → TRUE]  or  [CONDITION → FALSE]
Event:         [EVENT: DialogueEvent.Quest.Started | Payload: Quest.TreasureHunt]
SetVariable:   [SET: VarName = true]
SubDialogue:   [→ Entering sub-dialogue: GenericFarewell]
Pop:           [← Returning to: MyNPC_Intro]
Timeout:       [TIMEOUT: DefaultChoiceIndex = 0 auto-selected]
End:           [SESSION ENDED: Completed]
Error:         [ERROR: AssetError — exceeded MaxAutoSteps]
```

---

## Context Override — Requirement Integration

Condition nodes call `FDialogueSimulator::BuildContext(Session)`. In the editor, the session has no live `Chooser` actor. `BuildContext` is designed to handle a null Chooser gracefully: it builds an empty `FRequirementContext` and then merges the `FDialoguePreviewContext::ContextTags` into `FRequirementContext::SourceTags`.

This means a designer can set `Dialogue.HasQuest.TreasureHunt` in the tag override panel, and a `URequirement` that checks for that tag will evaluate correctly in the preview without a running game world.

```cpp
FRequirementContext FDialogueSimulator::BuildContext(const FDialogueSession& Session)
{
    FRequirementContext Ctx;
    if (AActor* Chooser = Session.GetChooser())
    {
        // Live session: build from actor.
        Ctx.SourceActor = Chooser;
        if (ITaggedInterface* Tagged = Cast<ITaggedInterface>(Chooser))
            Ctx.SourceTags = Tagged->GetOwnedTags();
    }
    // Merge session variable overrides into PersistedData for Condition node evaluation.
    for (const auto& Pair : Session.Variables)
    {
        // FDialogueVariant → FRequirementPayload conversion is variant-type-specific.
        // Bool and Int are stored as name-keyed payloads; Tag is added to SourceTags.
        if (Pair.Value.Type == EDialogueVariantType::Tag)
            Ctx.SourceTags.AddTag(Pair.Value.AsTag());
        else
            Ctx.PersistedData.Add(Pair.Key, FRequirementPayload::FromVariant(Pair.Value));
    }
    return Ctx;
}
```

---

## Build.cs

```csharp
// GameCoreEditor.Build.cs
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core", "CoreUObject", "Engine",
    "GameCore",           // For FDialogueSimulator, UDialogueAsset, all node types
    "UnrealEd",
    "AssetTools",
    "SlateCore", "Slate",
    "GameplayTags",
    "GameplayTagsEditor", // For SGameplayTagWidget in context override panel
});
```
