# GMS Events

**Sub-page of:** [Dialogue System](Dialogue%20System%20Overview.md)

All GameplayMessage events emitted by the Dialogue System. Game code registers listeners on these tags. The dialogue system never subscribes to any game-side event channel.

---

## Event Tags

All tags live under the `DialogueEvent` parent tag, defined in the GameCore tag file.

| Tag | Payload Struct | Fired by | When |
|---|---|---|---|
| `DialogueEvent.Session.Started` | `FDialogueSessionEventMessage` | `UDialogueComponent` | A session starts (after first `RunSession` call) |
| `DialogueEvent.Session.Ended` | `FDialogueSessionEndedMessage` | `UDialogueComponent` | A session ends for any reason |
| `DialogueEvent.Node.Event` | `FDialogueEventMessage` | `UDialogueNode_Event` | An Event node executes |

Game code uses `DialogueEvent.Node.Event` with specific `PayloadTag` values to differentiate events — e.g. `DialogueEvent.Node.Event` carrying `PayloadTag = Quest.Started.TreasureHunt`.

---

## Payload Structs

```cpp
// File: Dialogue/DialogueTypes.h

USTRUCT(BlueprintType)
struct FDialogueSessionEventMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FGuid SessionID;

    // The Chooser or sole instigator.
    UPROPERTY(BlueprintReadOnly)
    TWeakObjectPtr<AActor> Instigator;

    // The asset that started or ended.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UDialogueAsset> Asset;
};

USTRUCT(BlueprintType)
struct FDialogueSessionEndedMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FGuid SessionID;

    UPROPERTY(BlueprintReadOnly)
    TWeakObjectPtr<AActor> Instigator;

    UPROPERTY(BlueprintReadOnly)
    EDialogueEndReason Reason;

    // Optional reason tag from UDialogueNode_End::EndReasonTag.
    UPROPERTY(BlueprintReadOnly)
    FGameplayTag EndReasonTag;
};
```

> `FDialogueEventMessage` is defined in [Runtime Structs](Runtime%20Structs.md).

---

## Gameplay Tag Definitions

Add to the GameCore tag ini (`Config/Tags/GameCore.Dialogue.ini`):

```ini
[/Script/GameplayTags.GameplayTagsList]
+GameplayTagList=(Tag="DialogueEvent",DevComment="Root tag for all dialogue system GMS events")
+GameplayTagList=(Tag="DialogueEvent.Session",DevComment="Session lifecycle events")
+GameplayTagList=(Tag="DialogueEvent.Session.Started",DevComment="Session started")
+GameplayTagList=(Tag="DialogueEvent.Session.Ended",DevComment="Session ended")
+GameplayTagList=(Tag="DialogueEvent.Node",DevComment="Node execution events")
+GameplayTagList=(Tag="DialogueEvent.Node.Event",DevComment="UDialogueNode_Event fired")
+GameplayTagList=(Tag="DialogueEvent.End",DevComment="End reason tags for UDialogueNode_End")
+GameplayTagList=(Tag="Dialogue.Speaker",DevComment="Root tag for speaker identity tags")
```

Game projects add their own speaker and payload tags under `Dialogue.Speaker.*` and any payload tag namespace they choose.
