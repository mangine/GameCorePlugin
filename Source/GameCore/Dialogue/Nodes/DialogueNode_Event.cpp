// Copyright GameCore Plugin. All Rights Reserved.
#include "DialogueNode_Event.h"
#include "GameplayMessageSubsystem.h"
#include "Dialogue/DialogueTypes.h"

FDialogueStepResult UDialogueNode_Event::Execute(FDialogueSession& Session) const
{
    if (EventTag.IsValid())
    {
        UWorld* World = Session.GetWorld();
        if (World)
        {
            FDialogueEventMessage Msg;
            Msg.SessionID  = Session.SessionID;
            Msg.Instigator = Session.GetChooser();
            Msg.PayloadTag = PayloadTag;

            UGameplayMessageSubsystem::Get(World).BroadcastMessage(EventTag, Msg);
        }
    }

    FDialogueStepResult Result;
    Result.Action   = EDialogueStepAction::Continue;
    Result.NextNode = NextNodeIndex;
    return Result;
}

#if WITH_EDITOR
FString UDialogueNode_Event::GetPreviewLabel() const
{
    return FString::Printf(TEXT("Event: %s"), *EventTag.ToString());
}
#endif
