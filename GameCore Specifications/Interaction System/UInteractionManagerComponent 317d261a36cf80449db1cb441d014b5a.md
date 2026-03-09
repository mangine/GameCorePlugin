# UInteractionManagerComponent

**Sub-page of:** [Interaction System — Enhanced Specification](../Interaction%20System%20317d261a36cf8196ae77fc3c2e1e352d.md)

`UInteractionManagerComponent` is the **interaction authority for its pawn** — added to any actor that initiates interactions (players, NPCs). It owns the full interaction lifecycle: scanning for candidates, resolving options into prompts, handling input, running server-side validation, and broadcasting interaction outcomes. `UInteractionComponent` has no knowledge of this component — the relationship is one-way.

The scanning behaviour runs **exclusively on the owning client**. The RPC and validation behaviour runs on **both client and server** — the component exists on server pawns but with scanning fully suppressed.

**Files:** `Interaction/Components/InteractionManagerComponent.h / .cpp`

---

# Class Definition

```cpp
// Fires on the owning client when the best candidate component changes.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBestInteractableChanged,
    UInteractionComponent*, Previous,
    UInteractionComponent*, Current);

// Fires on the owning client whenever the resolved option list is rebuilt.
// UI widgets bind here to update the interaction prompt.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnResolvedOptionsChanged,
    const TArray<FResolvedInteractionOption>&, Options);

// Fires on the SERVER after all validation passes.
// Intended for PLAYER-SIDE systems that need to react to the confirmation (e.g. trigger
// animation, deduct stamina, update quest state on the player). For reacting on the
// interactable side (open shop, grant resource, start dialogue), bind to
// UInteractionComponent::OnInteractionExecuted on the interactable actor instead.
// Source: the UInteractionComponent that was interacted with.
// EntryIndex: the flat entry index within that component.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInteractionConfirmed,
    UInteractionComponent*, Source,
    uint8,                  EntryIndex);

// Fires on the owning client when the server confirms the interaction via ClientRPC.
// Use for client-side feedback (sound, animation, UI flash).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInteractionConfirmedClient,
    UInteractionComponent*, Source,
    uint8,                  EntryIndex);

// Fires on the owning client when the server rejects the interaction.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInteractionRejected,
    uint8,                       EntryIndex,
    EInteractionRejectionReason, Reason);

// Fires every Tick during an active Hold. Progress is [0, 1].
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHoldProgressChanged,
    float, Progress);

// Fires when a Hold is cancelled before completion.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHoldCancelled,
    uint8,             EntryIndex,
    EHoldCancelReason, Reason);

UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UInteractionManagerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UInteractionManagerComponent();

    // ── Scanner Configuration ─────────────────────────────────────────────────

    // How resolved options are shaped for the current best component.
    // Best: one winner per group (standard HUD prompt).
    // All: full sorted list including Locked entries (inspect / examine UIs).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner")
    EResolveMode ResolveMode = EResolveMode::Best;

    // Scan interval in seconds. Smaller = more responsive, higher CPU cost on clients.
    // Clamped to minimum 0.1s at BeginPlay.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner",
        meta = (ClampMin = "0.1"))
    float ScanPeriod = 0.2f;

    // Maximum sphere radius for the candidate overlap query.
    // Global ceiling — components beyond this distance are never detected regardless of
    // their own MaxInteractionDistance. Set to at least the largest MaxInteractionDistance
    // among interactables in the level.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner",
        meta = (ClampMin = "50.0"))
    float ScanDistance = 600.0f;

    // Candidate scoring weight between distance and view angle.
    // 0.0 = pure distance. 1.0 = pure camera angle. 0.6 suits third-person.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ViewAngleWeight = 0.6f;

    // Scanning and hold state machine are suppressed while the pawn owns any of these tags.
    // Current best is cleared immediately on suppression.
    // Use for: death, downed, cutscene, stunned, mounted, in dialogue.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner")
    FGameplayTagContainer DisablingTags;

    // Distance the pawn must move from the hold-start location to auto-cancel.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner|Hold",
        meta = (ClampMin = "5.0"))
    float HoldCancelMoveThreshold = 50.0f;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

    // ── Input API (call from Enhanced Input bindings) ─────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Interaction")
    void RequestInteract(uint8 EntryIndex);

    UFUNCTION(BlueprintCallable, Category = "Interaction")
    void RequestInteractRelease();

    // ── Server RPC ────────────────────────────────────────────────────────────

    // Called by RequestInteract on the owning client.
    // Routing: this component is on the pawn, which owns its net connection —
    // Server RPCs declared here route correctly on dedicated server.
    // ComponentRef is validated server-side before use.
    UFUNCTION(Server, Reliable)
    void ServerRequestInteract(UInteractionComponent* ComponentRef, uint8 EntryIndex);

    // ── Client RPCs ───────────────────────────────────────────────────────────

    UFUNCTION(Client, Reliable)
    void ClientRPC_OnInteractionConfirmed(UInteractionComponent* Source, uint8 EntryIndex);

    UFUNCTION(Client, Reliable)
    void ClientRPC_OnInteractionRejected(uint8 EntryIndex, EInteractionRejectionReason Reason);

    // ── Delegates ─────────────────────────────────────────────────────────────

    UPROPERTY(BlueprintAssignable, Category = "Interaction")
    FOnBestInteractableChanged OnBestInteractableChanged;

    UPROPERTY(BlueprintAssignable, Category = "Interaction")
    FOnResolvedOptionsChanged OnResolvedOptionsChanged;

    // SERVER only. Fires after validation for player-side systems (animations, quest tracking,
    // stamina costs on the instigator). For execution on the interactable actor itself,
    // bind to UInteractionComponent::OnInteractionExecuted instead.
    UPROPERTY(BlueprintAssignable, Category = "Interaction")
    FOnInteractionConfirmed OnInteractionConfirmed;

    UPROPERTY(BlueprintAssignable, Category = "Interaction")
    FOnInteractionConfirmedClient OnInteractionConfirmedClient;

    UPROPERTY(BlueprintAssignable, Category = "Interaction")
    FOnInteractionRejected OnInteractionRejected;

    UPROPERTY(BlueprintAssignable, Category = "Interaction")
    FOnHoldProgressChanged OnHoldProgressChanged;

    UPROPERTY(BlueprintAssignable, Category = "Interaction")
    FOnHoldCancelled OnHoldCancelled;

private:
    // ── Scanner Runtime State ─────────────────────────────────────────────────

    FTimerHandle ScanTimerHandle;

    // Cached once at BeginPlay — avoids Cast<ITaggedInterface> on every scan tick.
    ITaggedInterface* CachedTagInterface = nullptr;

    TObjectPtr<UInteractionComponent> CurrentBestComponent;

    // Pre-allocated buffers — no heap allocation on the scan hot path.
    TArray<FOverlapResult>             OverlapBuffer;
    TArray<FResolvedInteractionOption> ResolvedBuffer;

    // ── Hold State Machine ────────────────────────────────────────────────────

    EInteractionHoldState             HoldState           = EInteractionHoldState::Idle;
    uint8                             HoldEntryIndex      = 0;
    float                             HoldProgress        = 0.0f;
    float                             HoldAccumulatedTime = 0.0f;
    FVector                           HoldStartLocation   = FVector::ZeroVector;
    TObjectPtr<UInteractionComponent> HoldTargetComponent;

    // ── Internal Methods ──────────────────────────────────────────────────────

    void ExecuteScan();
    void RunResolve();
    void ClearCurrentBest();
    void TriggerImmediateRescan();
    void RefreshCachedTagInterface();
    bool IsDisabledByTag() const;

    void BeginHold(uint8 EntryIndex);
    void CompleteHold();
    void CancelHold(EHoldCancelReason Reason);

    UFUNCTION()
    void OnTrackedStateChanged(UInteractionComponent* Component, uint8 EntryIndex);
};
```

> **Exists on server pawns, but scanning is suppressed.** `BeginPlay` guards all scanning behaviour behind `IsLocallyControlled()`. On the server the component is inert as a scanner — no timer, no tick, no overlap queries. It exists on the server solely so that `ServerRequestInteract` and the Client RPCs declared here have the correct net connection to route through.

> **Execution flows through the interactable, not the pawn.** After validation passes, `ComponentRef->ExecuteEntry()` is called first — this dispatches `OnInteractionExecuted` on the interactable actor. Game systems (resource, shop, dialogue) bind there. `OnInteractionConfirmed` on the manager then fires for player-side reactions (animations, quest state, stamina costs on the instigator pawn).

---

# Hold Cancel Reason Enum

```cpp
// File: Interaction/Enums/InteractionEnums.h

UENUM(BlueprintType)
enum class EHoldCancelReason : uint8
{
    InputReleased UMETA(DisplayName = "Input Released"),
    PlayerMoved   UMETA(DisplayName = "Player Moved"),
    DisabledByTag UMETA(DisplayName = "Disabled By Tag"),
    TargetChanged UMETA(DisplayName = "Target Changed"),
    TargetLost    UMETA(DisplayName = "Target Lost"),
};
```

---

# BeginPlay & EndPlay

```cpp
void UInteractionManagerComponent::BeginPlay()
{
    Super::BeginPlay();

    // Scanning runs only on the owning client.
    // The component still exists on the server for RPC routing — just inert as a scanner.
    APawn* Pawn = Cast<APawn>(GetOwner());
    if (!Pawn || !Pawn->IsLocallyControlled())
        return;

    ScanPeriod = FMath::Max(ScanPeriod, 0.1f);
    RefreshCachedTagInterface();

    OverlapBuffer.Reserve(32);
    ResolvedBuffer.Reserve(8);

    // Tick drives the hold state machine only. Disabled at rest.
    SetComponentTickEnabled(false);
    PrimaryComponentTick.bStartWithTickEnabled = false;

    GetWorld()->GetTimerManager().SetTimer(
        ScanTimerHandle,
        this,
        &UInteractionManagerComponent::ExecuteScan,
        ScanPeriod,
        /*bLoop=*/true,
        /*FirstDelay=*/0.0f);
}

void UInteractionManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorld()->GetTimerManager().ClearTimer(ScanTimerHandle);
    ClearCurrentBest();
    Super::EndPlay(EndPlayReason);
}

void UInteractionManagerComponent::RefreshCachedTagInterface()
{
    CachedTagInterface = Cast<ITaggedInterface>(GetOwner());
}
```

> **`PrimaryComponentTick.bCanEverTick = true` must be set in the constructor** alongside `bStartWithTickEnabled = false`. Without `bCanEverTick`, calling `SetComponentTickEnabled(true)` during a hold has no effect and the hold state machine never runs.

---

# Scan Execution

```cpp
void UInteractionManagerComponent::ExecuteScan()
{
    APawn* Pawn = Cast<APawn>(GetOwner());
    if (!Pawn) return;

    // [1] Disabling tag check — fast path for empty container.
    if (IsDisabledByTag())
    {
        ClearCurrentBest();
        return;
    }

    // [2] Sphere overlap on the Interaction collision channel.
    const FVector PawnLocation = Pawn->GetActorLocation();
    OverlapBuffer.Reset();
    GetWorld()->SweepMultiByChannel(
        OverlapBuffer,
        PawnLocation, PawnLocation,
        FQuat::Identity,
        ECC_GameTraceChannel_Interaction,
        FCollisionShape::MakeSphere(ScanDistance));

    // [3] Score candidates — deduplicate by actor first.
    TMap<AActor*, float>                ActorScores;
    TMap<AActor*, UInteractionComponent*> ActorComponents;
    ActorScores.Reserve(OverlapBuffer.Num());

    APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
    FVector CameraForward = FVector::ForwardVector;
    if (PC && PC->PlayerCameraManager)
        CameraForward = PC->PlayerCameraManager->GetCameraRotation().Vector();

    for (const FOverlapResult& Hit : OverlapBuffer)
    {
        AActor* HitActor = Hit.GetActor();
        if (!HitActor || ActorScores.Contains(HitActor)) continue;

        UInteractionComponent* Comp = HitActor->FindComponentByClass<UInteractionComponent>();
        if (!Comp) continue;

        const FVector ClosestPoint = Hit.Component.IsValid()
            ? Hit.Component->GetComponentLocation()
            : HitActor->GetActorLocation();

        const float Distance = FVector::Dist(ClosestPoint, PawnLocation);
        if (Distance > Comp->MaxInteractionDistance) continue;

        const float DistScore  = 1.0f - FMath::Clamp(Distance / ScanDistance, 0.0f, 1.0f);
        const FVector ToTarget = (ClosestPoint - PawnLocation).GetSafeNormal();
        const float AngleScore = (FVector::DotProduct(CameraForward, ToTarget) + 1.0f) * 0.5f;
        const float FinalScore = FMath::Lerp(DistScore, AngleScore, ViewAngleWeight);

        ActorScores.Add(HitActor, FinalScore);
        ActorComponents.Add(HitActor, Comp);
    }

    // [4] Select winner.
    UInteractionComponent* BestComp = nullptr;
    float BestScore = -1.0f;
    for (auto& Pair : ActorScores)
    {
        if (Pair.Value > BestScore) { BestScore = Pair.Value; BestComp = ActorComponents[Pair.Key]; }
    }

    // [5] Handle candidate change.
    if (BestComp != CurrentBestComponent)
    {
        UInteractionComponent* Previous = CurrentBestComponent;

        if (CurrentBestComponent)
        {
            CurrentBestComponent->OnEntryStateChanged.RemoveDynamic(
                this, &UInteractionManagerComponent::OnTrackedStateChanged);

            if (HoldState == EInteractionHoldState::Holding &&
                HoldTargetComponent == CurrentBestComponent)
                CancelHold(EHoldCancelReason::TargetChanged);
        }

        CurrentBestComponent = BestComp;

        if (BestComp)
            BestComp->OnEntryStateChanged.AddDynamic(
                this, &UInteractionManagerComponent::OnTrackedStateChanged);

        OnBestInteractableChanged.Broadcast(Previous, BestComp);
        RunResolve();
    }
    // [6] No change — OnTrackedStateChanged handles state-driven re-resolves.
}

bool UInteractionManagerComponent::IsDisabledByTag() const
{
    if (DisablingTags.IsEmpty()) return false;
    if (!CachedTagInterface)    return false;
    return CachedTagInterface->GetGameplayTags().HasAny(DisablingTags);
}

void UInteractionManagerComponent::ClearCurrentBest()
{
    if (!CurrentBestComponent) return;

    CurrentBestComponent->OnEntryStateChanged.RemoveDynamic(
        this, &UInteractionManagerComponent::OnTrackedStateChanged);

    UInteractionComponent* Previous = CurrentBestComponent;
    CurrentBestComponent = nullptr;

    if (HoldState == EInteractionHoldState::Holding)
        CancelHold(EHoldCancelReason::TargetLost);

    ResolvedBuffer.Reset();
    OnBestInteractableChanged.Broadcast(Previous, nullptr);
    OnResolvedOptionsChanged.Broadcast(ResolvedBuffer);
}

void UInteractionManagerComponent::RunResolve()
{
    if (!CurrentBestComponent)
    {
        ResolvedBuffer.Reset();
        OnResolvedOptionsChanged.Broadcast(ResolvedBuffer);
        return;
    }
    CurrentBestComponent->ResolveOptions(
        GetOwner(), CurrentBestComponent->GetOwner(), ResolveMode, ResolvedBuffer);
    OnResolvedOptionsChanged.Broadcast(ResolvedBuffer);
}

void UInteractionManagerComponent::OnTrackedStateChanged(
    UInteractionComponent* /*Component*/, uint8 /*EntryIndex*/)
{
    RunResolve();
}
```

---

# Hold State Machine

Runs on `TickComponent`, enabled only while `HoldState == Holding`. Zero tick overhead at rest.

```cpp
void UInteractionManagerComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!HoldTargetComponent || CurrentBestComponent != HoldTargetComponent)
    {
        CancelHold(EHoldCancelReason::TargetLost);
        return;
    }
    if (IsDisabledByTag())
    {
        CancelHold(EHoldCancelReason::DisabledByTag);
        return;
    }

    APawn* Pawn = Cast<APawn>(GetOwner());
    if (Pawn)
    {
        const float MovedSq = FVector::DistSquared(Pawn->GetActorLocation(), HoldStartLocation);
        if (MovedSq > FMath::Square(HoldCancelMoveThreshold))
        {
            CancelHold(EHoldCancelReason::PlayerMoved);
            return;
        }
    }

    const FInteractionEntryConfig* Config =
        HoldTargetComponent->GetConfigAtIndex(HoldEntryIndex);
    if (!Config || Config->HoldTimeSeconds <= 0.0f)
    {
        CancelHold(EHoldCancelReason::TargetLost);
        return;
    }

    HoldAccumulatedTime += DeltaTime;
    HoldProgress = FMath::Clamp(HoldAccumulatedTime / Config->HoldTimeSeconds, 0.0f, 1.0f);
    OnHoldProgressChanged.Broadcast(HoldProgress);

    if (HoldProgress >= 1.0f)
        CompleteHold();
}

void UInteractionManagerComponent::BeginHold(uint8 EntryIndex)
{
    APawn* Pawn = Cast<APawn>(GetOwner());
    HoldState           = EInteractionHoldState::Holding;
    HoldEntryIndex      = EntryIndex;
    HoldProgress        = 0.0f;
    HoldAccumulatedTime = 0.0f;
    HoldStartLocation   = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
    HoldTargetComponent = CurrentBestComponent;
    SetComponentTickEnabled(true);
    OnHoldProgressChanged.Broadcast(0.0f);
}

void UInteractionManagerComponent::CompleteHold()
{
    SetComponentTickEnabled(false);
    HoldState = EInteractionHoldState::Completed;
    OnHoldProgressChanged.Broadcast(1.0f);

    // Clear before RPC — prevents double-cancel if a state change arrives during RPC processing.
    UInteractionComponent* Target = HoldTargetComponent;
    HoldTargetComponent = nullptr;
    HoldState = EInteractionHoldState::Idle;

    if (Target)
        ServerRequestInteract(Target, HoldEntryIndex);

    TriggerImmediateRescan();
}

void UInteractionManagerComponent::CancelHold(EHoldCancelReason Reason)
{
    if (HoldState == EInteractionHoldState::Idle) return;
    SetComponentTickEnabled(false);
    HoldTargetComponent = nullptr;
    HoldState = EInteractionHoldState::Idle;
    OnHoldProgressChanged.Broadcast(0.0f);
    OnHoldCancelled.Broadcast(HoldEntryIndex, Reason);
}
```

---

# Interaction Request

```cpp
void UInteractionManagerComponent::RequestInteract(uint8 EntryIndex)
{
    if (!CurrentBestComponent) return;

    const FInteractionEntryNetState* NetState =
        CurrentBestComponent->GetNetStateAtIndex(EntryIndex);
    if (!NetState) return;

    // Client pre-check: filter trivially-rejectable states to avoid sending RPCs
    // the server would immediately reject. Does not replace server validation.
    if (!NetState->bServerEnabled                           ||
        NetState->State == EInteractableState::Disabled     ||
        NetState->State == EInteractableState::Occupied     ||
        NetState->State == EInteractableState::Cooldown)
        return;

    // Also filter Locked resolved options — no point sending an RPC for a locked entry.
    const FResolvedInteractionOption* Option = ResolvedBuffer.FindByPredicate(
        [EntryIndex](const FResolvedInteractionOption& O) { return O.EntryIndex == EntryIndex; });
    if (Option && Option->State == EInteractableState::Locked)
        return;

    const FInteractionEntryConfig* Config =
        CurrentBestComponent->GetConfigAtIndex(EntryIndex);
    if (!Config) return;

    if (Config->InputType == EInteractionInputType::Press)
    {
        ServerRequestInteract(CurrentBestComponent, EntryIndex);
        TriggerImmediateRescan();
    }
    else
    {
        BeginHold(EntryIndex);
    }
}

void UInteractionManagerComponent::RequestInteractRelease()
{
    if (HoldState == EInteractionHoldState::Holding)
        CancelHold(EHoldCancelReason::InputReleased);
}

void UInteractionManagerComponent::TriggerImmediateRescan()
{
    GetWorld()->GetTimerManager().ClearTimer(ScanTimerHandle);
    TWeakObjectPtr<UInteractionManagerComponent> WeakThis(this);
    GetWorld()->GetTimerManager().SetTimerForNextTick([WeakThis]()
    {
        if (UInteractionManagerComponent* Mgr = WeakThis.Get())
        {
            Mgr->ExecuteScan();
            GetWorld()->GetTimerManager().SetTimer(
                Mgr->ScanTimerHandle, Mgr,
                &UInteractionManagerComponent::ExecuteScan,
                Mgr->ScanPeriod, /*bLoop=*/true);
        }
    });
}
```

---

# Server RPC & Validation

```cpp
void UInteractionManagerComponent::ServerRequestInteract_Implementation(
    UInteractionComponent* ComponentRef, uint8 EntryIndex)
{
    // Validate the component reference — never trust a client-supplied pointer.
    // UE resolves replicated object references in RPCs; an invalid pointer arrives as null.
    if (!ComponentRef || !ComponentRef->GetOwner()) return;

    APawn* InstigatorPawn = Cast<APawn>(GetOwner());
    if (!InstigatorPawn) return;

    // [1] Entry exists
    if (static_cast<int32>(EntryIndex) >= ComponentRef->GetTotalEntryCount())
    {
        ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::EntryNotFound);
        return;
    }

    const FInteractionEntryNetState* NetState = ComponentRef->GetNetStateAtIndex(EntryIndex);
    const FInteractionEntryConfig*   Config   = ComponentRef->GetConfigAtIndex(EntryIndex);
    if (!NetState || !Config) return;

    // [2] Server-enabled check
    if (!NetState->bServerEnabled)
    {
        ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::EntryUnavailable);
        return;
    }

    // [3] Gameplay state
    if (NetState->State == EInteractableState::Occupied  ||
        NetState->State == EInteractableState::Cooldown  ||
        NetState->State == EInteractableState::Disabled)
    {
        ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::EntryUnavailable);
        return;
    }

    // [4] Distance check — authoritative. 75cm tolerance absorbs latency desync.
    const float DistSq = FVector::DistSquared(
        InstigatorPawn->GetActorLocation(), ComponentRef->GetOwner()->GetActorLocation());
    if (DistSq > FMath::Square(ComponentRef->MaxInteractionDistance + 75.0f))
    {
        ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::OutOfRange);
        return;
    }

    // [5] Source tag checks
    if (ITaggedInterface* Tags = Cast<ITaggedInterface>(InstigatorPawn))
    {
        const FGameplayTagContainer& T = Tags->GetGameplayTags();
        if (Config->SourceRequiredTags.Num() > 0 && !T.HasAll(Config->SourceRequiredTags))
        { ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::TagMismatch); return; }
        if (!Config->SourceTagQuery.IsEmpty() && !Config->SourceTagQuery.Matches(T))
        { ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::TagMismatch); return; }
    }

    // [6] Target tag checks
    if (ITaggedInterface* Tags = Cast<ITaggedInterface>(ComponentRef->GetOwner()))
    {
        const FGameplayTagContainer& T = Tags->GetGameplayTags();
        if (Config->TargetRequiredTags.Num() > 0 && !T.HasAll(Config->TargetRequiredTags))
        { ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::TagMismatch); return; }
        if (!Config->TargetTagQuery.IsEmpty() && !Config->TargetTagQuery.Matches(T))
        { ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::TagMismatch); return; }
    }

    // [7] Source condition provider
    if (IInteractionConditionProvider* P = Cast<IInteractionConditionProvider>(InstigatorPawn))
    {
        FInteractionConditionResult R = P->CanBeginInteraction(ComponentRef->GetOwner(), *Config);
        if (!R.bCanProceed)
        { ClientRPC_OnInteractionRejected(EntryIndex, R.RejectionReason); return; }
    }

    // [8] Target condition provider
    if (IInteractionConditionProvider* P = Cast<IInteractionConditionProvider>(ComponentRef->GetOwner()))
    {
        FInteractionConditionResult R = P->CanBeginInteraction(InstigatorPawn, *Config);
        if (!R.bCanProceed)
        { ClientRPC_OnInteractionRejected(EntryIndex, R.RejectionReason); return; }
    }

    // [9] All checks passed — execute.
    // First: dispatch to the interactable component so it can notify bound game systems
    // (resource system, shop system, dialogue system, etc.) via OnInteractionExecuted.
    ComponentRef->ExecuteEntry(EntryIndex, InstigatorPawn);

    // Second: notify player-side systems (animation, quest, UI) via the manager delegate.
    OnInteractionConfirmed.Broadcast(ComponentRef, EntryIndex);
    ClientRPC_OnInteractionConfirmed(ComponentRef, EntryIndex);
}

void UInteractionManagerComponent::ClientRPC_OnInteractionConfirmed_Implementation(
    UInteractionComponent* Source, uint8 EntryIndex)
{
    OnInteractionConfirmedClient.Broadcast(Source, EntryIndex);
    TriggerImmediateRescan();
}

void UInteractionManagerComponent::ClientRPC_OnInteractionRejected_Implementation(
    uint8 EntryIndex, EInteractionRejectionReason Reason)
{
    OnInteractionRejected.Broadcast(EntryIndex, Reason);
    TriggerImmediateRescan();
}
```

> **Validation lives entirely on this component.** `UInteractionComponent` is never called during validation — only its data is read (`GetConfigAtIndex`, `GetNetStateAtIndex`, `MaxInteractionDistance`). The component has no awareness that validation is happening.

> **`ComponentRef` is validated via null check only.** UE's RPC system resolves replicated object references server-side — a client cannot fabricate an arbitrary pointer to an actor the server doesn't know about. If the object is not replicated to the server or has been destroyed, the reference arrives as null and the early return handles it. The distance check at step [4] then provides the authoritative proximity gate.

---

# Debug Visualization

Controlled by CVar — active only in non-shipping builds:

```cpp
static TAutoConsoleVariable<int32> CVarInteractionDebug(
    TEXT("gc.Interaction.Debug"), 0,
    TEXT("0=off | 1=scan sphere + best candidate | 2=all candidates with scores and labels"),
    ECVF_Cheat);
```

| Level | Draws |
| --- | --- |
| 1 | Scan sphere (green = active, red = suppressed). Line to current best. Hold progress arc during active hold. |
| 2 | All scored candidates. Color by state: green=Available, yellow=Occupied, orange=Cooldown, grey=Locked. Score value and resolved labels above each. |

---

# Known Constraints

**`IsLocallyControlled()` is the only scanning guard.** If the pawn's controller changes at runtime (possession change, AI takeover), the scan timer may continue running on a non-owning machine or stop on the owning machine. Pawns with dynamic possession should bind to `APawn::ReceivePossessed` / `ReceiveUnpossessed` and restart the scan timer accordingly.

**`CachedTagInterface` is set once at `BeginPlay`.** If the pawn's `ITaggedInterface` implementation changes at runtime, call `RefreshCachedTagInterface()` manually.

**`ScanDistance` must be ≥ the largest `MaxInteractionDistance` in the level.** If a component's `MaxInteractionDistance` exceeds `ScanDistance`, it will never be detected by the scan even though the server would accept the interaction. Default 600cm covers the default 300cm component distance with generous margin.

**Non-player pawns.** `ServerRequestInteract_Implementation` does not check for a `PlayerController` — it operates on any pawn. NPC interactors can use this component directly with no changes.
