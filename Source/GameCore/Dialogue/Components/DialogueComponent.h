// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Dialogue/DialogueEnums.h"
#include "Dialogue/DialogueTypes.h"
#include "DialogueComponent.generated.h"

class UDialogueAsset;
class UDialogueManagerComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDialogueStarted,
    FGuid,             SessionID,
    AActor*,           Instigator);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDialogueEnded,
    FGuid,             SessionID,
    EDialogueEndReason, Reason);

// Server-authoritative session host.
// Owns all active FDialogueSession instances for its actor.
// Replicated so its NetGUID is valid on clients (required for FDialogueClientState::OwnerComponent).
//
// Net role: Server-authoritative. The component is replicated (SetIsReplicated(true))
// so its NetGUID is valid on clients, which is required for FDialogueClientState::OwnerComponent
// to deserialize correctly across RPCs.
UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UDialogueComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDialogueComponent();

    // ── Configuration ────────────────────────────────────────────────────────

    // If true, starting a new session for an instigator that already has an active
    // session ends the existing one. If false, the call is a silent no-op.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
    bool bInterruptOnNewSession = false;

    // ── Activation API (server only) ─────────────────────────────────────────

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dialogue")
    void StartDialogue(AActor* Instigator, UDialogueAsset* Asset);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dialogue")
    void StartGroupDialogue(const TArray<AActor*>& Participants, UDialogueAsset* Asset);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dialogue")
    void ForceEndSession(FGuid SessionID);

    UFUNCTION(BlueprintPure, BlueprintAuthorityOnly, Category = "Dialogue")
    bool HasActiveSession(const AActor* Instigator) const;

    // ── Chooser Resolution ───────────────────────────────────────────────────

    // Defaults to returning Participants[0].
    // Game code overrides to implement group-leader-as-Chooser or similar policies.
    DECLARE_DELEGATE_RetVal_OneParam(AActor*, FResolveChooserDelegate, const TArray<AActor*>&);
    FResolveChooserDelegate ResolveChooser;

    // ── Delegates ────────────────────────────────────────────────────────────

    UPROPERTY(BlueprintAssignable, Category = "Dialogue")
    FOnDialogueStarted OnDialogueStarted;

    UPROPERTY(BlueprintAssignable, Category = "Dialogue")
    FOnDialogueEnded OnDialogueEnded;

    // ── UActorComponent ──────────────────────────────────────────────────────

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    // Active sessions keyed by SessionID.
    TMap<FGuid, FDialogueSession> ActiveSessions;

    // ── Internal Flow ────────────────────────────────────────────────────────

    void RunSession(FDialogueSession& Session);
    void PushClientState(FDialogueSession& Session, const FDialogueStepResult& StepResult);
    void EndSession(FGuid SessionID, EDialogueEndReason Reason);
    void StartTimeoutTimer(FDialogueSession& Session, float TimeoutSeconds, int32 DefaultChoiceIndex);
    void OnChoiceTimeout(FGuid SessionID, int32 DefaultChoiceIndex);

    FDialogueClientState BuildClientState(
        const FDialogueSession& Session,
        const FDialogueStepResult& StepResult,
        bool bIsObserver) const;

    static UDialogueManagerComponent* GetManagerComponent(AActor* Actor);

    FGuid GetSessionIDForInstigator(const AActor* Instigator) const;

    // ── Called by UDialogueManagerComponent (direct C++ calls, not RPCs) ────

    void Server_ReceiveChoice(FGuid SessionID, AActor* Sender, int32 ChoiceIndex);
    void Server_ReceiveACK(FGuid SessionID, AActor* Sender);
    void Server_NotifyChooserDisconnect(FGuid SessionID);

    friend class UDialogueManagerComponent;
};
