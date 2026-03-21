# `UInteractionManagerComponent`

**File:** `Interaction/Components/InteractionManagerComponent.h/.cpp`

**Interaction authority for its pawn.** Owns the full interaction lifecycle: scanning, option resolution, input handling, server-side validation, and outcome broadcasting. Added to any actor that initiates interactions (players, NPCs). Fully inert on the server and non-owning clients as a scanner — exists on server pawns only for RPC routing.

---

## Class Definition

```cpp
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interaction/Enums/InteractionEnums.h"
#include "Interaction/ResolvedInteractionOption.h"
#include "InteractionManagerComponent.generated.h"

class UInteractionComponent;
class UInteractionDescriptorSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBestInteractableChanged,
    UInteractionComponent*, Previous,
    UInteractionComponent*, Current);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnResolvedOptionsChanged,
    const TArray<FResolvedInteractionOption>&, Options);

// SERVER only. Fires after all validation passes.
// Use for player-side reactions (animation, quest tracking, stamina costs on instigator).
// For reactions on the interactable itself, bind UInteractionComponent::OnInteractionExecuted.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInteractionConfirmed,
    UInteractionComponent*, Source,
    uint8,                  EntryIndex);

// Owning client only. Fires after server confirms via ClientRPC.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInteractionConfirmedClient,
    UInteractionComponent*, Source,
    uint8,                  EntryIndex);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInteractionRejected,
    uint8,                       EntryIndex,
    EInteractionRejectionReason, Reason);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHoldProgressChanged,
    float, Progress);

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

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner")
    EResolveMode ResolveMode = EResolveMode::Best;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner",
        meta = (ClampMin = "0.1"))
    float ScanPeriod = 0.2f;

    // Global ceiling — components beyond this are never detected.
    // Must be >= the largest MaxInteractionDistance among interactables in the level.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner",
        meta = (ClampMin = "50.0"))
    float ScanDistance = 600.0f;

    // 0.0 = pure distance scoring. 1.0 = pure camera angle. 0.6 suits third-person.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ViewAngleWeight = 0.6f;

    // Scanning and hold suppressed while pawn owns any of these tags.
    // Current best cleared immediately on suppression.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scanner")
    FGameplayTagContainer DisablingTags;

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
    FTimerHandle ScanTimerHandle;

    ITaggedInterface* CachedTagInterface = nullptr;

    // Cached at BeginPlay to avoid repeated subsystem lookups in ResolveOptions.
    TObjectPtr<UInteractionDescriptorSubsystem> DescriptorSubsystem;

    TObjectPtr<UInteractionComponent> CurrentBestComponent;

    // Pre-allocated buffers — no heap allocation on the scan hot path.
    TArray<FOverlapResult>             OverlapBuffer;
    TArray<FResolvedInteractionOption> ResolvedBuffer;

    EInteractionHoldState             HoldState           = EInteractionHoldState::Idle;
    uint8                             HoldEntryIndex      = 0;
    float                             HoldProgress        = 0.0f;
    float                             HoldAccumulatedTime = 0.0f;
    FVector                           HoldStartLocation   = FVector::ZeroVector;
    TObjectPtr<UInteractionComponent> HoldTargetComponent;

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

---

## BeginPlay & EndPlay

```cpp
void UInteractionManagerComponent::BeginPlay()
{
    Super::BeginPlay();

    APawn* Pawn = Cast<APawn>(GetOwner());
    if (!Pawn || !Pawn->IsLocallyControlled())
        return;

    ScanPeriod = FMath::Max(ScanPeriod, 0.1f);
    RefreshCachedTagInterface();

    DescriptorSubsystem = GetGameInstance()
        ->GetSubsystem<UInteractionDescriptorSubsystem>();

    OverlapBuffer.Reserve(32);
    ResolvedBuffer.Reserve(8);

    // Tick drives hold state machine only. Off at rest.
    SetComponentTickEnabled(false);
    PrimaryComponentTick.bStartWithTickEnabled = false;
    // bCanEverTick must be true in constructor for SetComponentTickEnabled to work.

    GetWorld()->GetTimerManager().SetTimer(
        ScanTimerHandle, this,
        &UInteractionManagerComponent::ExecuteScan,
        ScanPeriod, /*bLoop=*/true, /*FirstDelay=*/0.0f);
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

> `PrimaryComponentTick.bCanEverTick = true` must be set in the constructor, alongside `bStartWithTickEnabled = false`. Without it, `SetComponentTickEnabled(true)` during a hold has no effect.

---

## Scan Execution

```cpp
void UInteractionManagerComponent::ExecuteScan()
{
    APawn* Pawn = Cast<APawn>(GetOwner());
    if (!Pawn) return;

    if (IsDisabledByTag()) { ClearCurrentBest(); return; }

    const FVector PawnLocation = Pawn->GetActorLocation();
    OverlapBuffer.Reset();
    GetWorld()->SweepMultiByChannel(
        OverlapBuffer, PawnLocation, PawnLocation,
        FQuat::Identity, ECC_GameTraceChannel_Interaction,
        FCollisionShape::MakeSphere(ScanDistance));

    // Score candidates — deduplicate by actor.
    TMap<AActor*, float>                  ActorScores;
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

        UInteractionComponent* Comp =
            HitActor->FindComponentByClass<UInteractionComponent>();
        if (!Comp) continue;

        const FVector ClosestPoint = Hit.Component.IsValid()
            ? Hit.Component->GetComponentLocation()
            : HitActor->GetActorLocation();

        const float Distance = FVector::Dist(ClosestPoint, PawnLocation);
        if (Distance > Comp->MaxInteractionDistance) continue;

        const float DistScore  = 1.0f - FMath::Clamp(Distance / ScanDistance, 0.0f, 1.0f);
        const FVector ToTarget = (ClosestPoint - PawnLocation).GetSafeNormal();
        const float AngleScore = (FVector::DotProduct(CameraForward, ToTarget) + 1.0f) * 0.5f;

        ActorScores.Add(HitActor, FMath::Lerp(DistScore, AngleScore, ViewAngleWeight));
        ActorComponents.Add(HitActor, Comp);
    }

    // Select winner.
    UInteractionComponent* BestComp = nullptr;
    float BestScore = -1.0f;
    for (auto& Pair : ActorScores)
        if (Pair.Value > BestScore) { BestScore = Pair.Value; BestComp = ActorComponents[Pair.Key]; }

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

        // Update highlight
        if (Previous)
            if (auto* H = Previous->GetOwner()->FindComponentByClass<UHighlightComponent>())
                H->SetHighlightActive(false);
        if (BestComp)
            if (auto* H = BestComp->GetOwner()->FindComponentByClass<UHighlightComponent>())
                H->SetHighlightActive(true);

        OnBestInteractableChanged.Broadcast(Previous, BestComp);
        RunResolve();
    }
}

bool UInteractionManagerComponent::IsDisabledByTag() const
{
    if (DisablingTags.IsEmpty() || !CachedTagInterface) return false;
    return CachedTagInterface->GetGameplayTags().HasAny(DisablingTags);
}

void UInteractionManagerComponent::ClearCurrentBest()
{
    if (!CurrentBestComponent) return;

    CurrentBestComponent->OnEntryStateChanged.RemoveDynamic(
        this, &UInteractionManagerComponent::OnTrackedStateChanged);

    if (auto* H = CurrentBestComponent->GetOwner()->FindComponentByClass<UHighlightComponent>())
        H->SetHighlightActive(false);

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
    // Populate UIDescriptor per option
    for (FResolvedInteractionOption& Option : ResolvedBuffer)
    {
        const FInteractionEntryConfig* Config =
            CurrentBestComponent->GetConfigAtIndex(Option.EntryIndex);
        if (Config && Config->UIDescriptorClass && DescriptorSubsystem)
            Option.UIDescriptor = DescriptorSubsystem->GetOrCreate(Config->UIDescriptorClass);
    }
    OnResolvedOptionsChanged.Broadcast(ResolvedBuffer);
}

void UInteractionManagerComponent::OnTrackedStateChanged(
    UInteractionComponent*, uint8)
{
    RunResolve();
}
```

---

## Hold State Machine

```cpp
void UInteractionManagerComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!HoldTargetComponent || CurrentBestComponent != HoldTargetComponent)
    { CancelHold(EHoldCancelReason::TargetLost); return; }
    if (IsDisabledByTag())
    { CancelHold(EHoldCancelReason::DisabledByTag); return; }

    APawn* Pawn = Cast<APawn>(GetOwner());
    if (Pawn)
    {
        const float MovedSq = FVector::DistSquared(Pawn->GetActorLocation(), HoldStartLocation);
        if (MovedSq > FMath::Square(HoldCancelMoveThreshold))
        { CancelHold(EHoldCancelReason::PlayerMoved); return; }
    }

    const FInteractionEntryConfig* Config =
        HoldTargetComponent->GetConfigAtIndex(HoldEntryIndex);
    if (!Config || Config->HoldTimeSeconds <= 0.0f)
    { CancelHold(EHoldCancelReason::TargetLost); return; }

    HoldAccumulatedTime += DeltaTime;
    HoldProgress = FMath::Clamp(HoldAccumulatedTime / Config->HoldTimeSeconds, 0.0f, 1.0f);
    OnHoldProgressChanged.Broadcast(HoldProgress);

    if (HoldProgress >= 1.0f) CompleteHold();
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

## Interaction Request

```cpp
void UInteractionManagerComponent::RequestInteract(uint8 EntryIndex)
{
    if (!CurrentBestComponent) return;

    const FInteractionEntryNetState* NetState =
        CurrentBestComponent->GetNetStateAtIndex(EntryIndex);
    if (!NetState) return;

    // Client pre-check: filter trivially rejectable states before RPC.
    if (!NetState->bServerEnabled                            ||
        NetState->State == EInteractableState::Disabled      ||
        NetState->State == EInteractableState::Occupied      ||
        NetState->State == EInteractableState::Cooldown)
        return;

    // Don't RPC for Locked resolved options.
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
            Mgr->GetWorld()->GetTimerManager().SetTimer(
                Mgr->ScanTimerHandle, Mgr,
                &UInteractionManagerComponent::ExecuteScan,
                Mgr->ScanPeriod, /*bLoop=*/true);
        }
    });
}
```

---

## Server RPC & Validation

```cpp
void UInteractionManagerComponent::ServerRequestInteract_Implementation(
    UInteractionComponent* ComponentRef, uint8 EntryIndex)
{
    if (!ComponentRef || !ComponentRef->GetOwner()) return;

    APawn* InstigatorPawn = Cast<APawn>(GetOwner());
    if (!InstigatorPawn) return;

    // [1] Entry exists
    if (static_cast<int32>(EntryIndex) >= ComponentRef->GetTotalEntryCount())
    { ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::EntryNotFound); return; }

    const FInteractionEntryNetState* NetState = ComponentRef->GetNetStateAtIndex(EntryIndex);
    const FInteractionEntryConfig*   Config   = ComponentRef->GetConfigAtIndex(EntryIndex);
    if (!NetState || !Config) return;

    // [2] Server-enabled
    if (!NetState->bServerEnabled)
    { ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::EntryUnavailable); return; }

    // [3] Gameplay state
    if (NetState->State == EInteractableState::Occupied  ||
        NetState->State == EInteractableState::Cooldown  ||
        NetState->State == EInteractableState::Disabled)
    { ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::EntryUnavailable); return; }

    // [4] Distance + 75cm latency tolerance
    const float DistSq = FVector::DistSquared(
        InstigatorPawn->GetActorLocation(), ComponentRef->GetInteractionLocation());
    if (DistSq > FMath::Square(ComponentRef->MaxInteractionDistance + 75.0f))
    { ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::OutOfRange); return; }

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

    // [7] Entry requirements
    if (Config->EntryRequirements)
    {
        FRequirementContext Context;
        Context.Instigator  = InstigatorPawn;
        Context.PlayerState = InstigatorPawn->GetPlayerState();
        Context.World       = GetWorld();
        FRequirementResult Result = Config->EntryRequirements->Evaluate(Context);
        if (!Result.bPassed)
        { ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::ConditionFailed); return; }
    }

    // All checks passed.
    ComponentRef->ExecuteEntry(EntryIndex, InstigatorPawn);           // interactable-side execution
    OnInteractionConfirmed.Broadcast(ComponentRef, EntryIndex);       // player-side systems
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

---

## Debug Visualization

```cpp
static TAutoConsoleVariable<int32> CVarInteractionDebug(
    TEXT("gc.Interaction.Debug"), 0,
    TEXT("0=off | 1=scan sphere + best candidate | 2=all candidates with scores"),
    ECVF_Cheat);
```

| Level | Draws |
|---|---|
| 1 | Scan sphere (green=active, red=suppressed). Line to current best. Hold progress arc. |
| 2 | All scored candidates. Color by state. Score value and resolved labels. |

---

## Constraints

- **`IsLocallyControlled()` is the only scanning guard.** Pawns with dynamic possession should restart the scan timer on `ReceivePossessed` / `ReceiveUnpossessed`.
- **`CachedTagInterface` is set once at `BeginPlay`.** Call `RefreshCachedTagInterface()` if the pawn's `ITaggedInterface` changes at runtime.
- **`ScanDistance` must be ≥ the largest `MaxInteractionDistance` in the level.** Default 600cm covers the default 300cm component distance with margin.
- **Non-player pawns.** `ServerRequestInteract_Implementation` does not require a `PlayerController` — NPC interactors can use this component directly.
