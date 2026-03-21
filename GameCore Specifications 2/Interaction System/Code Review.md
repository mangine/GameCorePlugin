# Interaction System — Code Review

---

## Overview

The Interaction System is well-conceived overall: the separation between static config and replicated state is sound, FFastArraySerializer with Push Model is the correct replication pattern at MMO scale, and the server-only validation architecture is solid. The issues below are targeted — mostly integration inconsistencies, minor architecture gaps, and one notable functional concern.

---

## Issues

### 1. `TArray<URequirement*>` on config should be a `URequirementList` reference

**Severity: Medium**

The original spec stores `TArray<TObjectPtr<URequirement>> EntryRequirements` directly on `FInteractionEntryConfig`. This bypasses the Requirement System's established API (`URequirementList::Evaluate`, `ERequirementEvalAuthority`, `ERequirementListOperator`) and duplicates what `URequirementList` already provides.

The Requirement System v2 centralizes authority declaration and operator configuration on `URequirementList`. Using a raw array means:
- The interaction system must re-implement operator logic (AND short-circuit).
- The authority declaration (`ClientValidated`) has no formal home — it is only a convention in documentation.
- Designers cannot share requirement lists across entries or systems.

**Fix (applied in v2 spec):** `EntryRequirements` is a `TObjectPtr<URequirementList>` soft reference. Authority is encoded on the list asset by the designer. The system calls `List->Evaluate(Context)` on both client and server. This is consistent with how every other system in GameCore uses requirements.

---

### 2. `ResolveOptions` mixes resolution logic with descriptor population

**Severity: Low**

The original spec passes `UIDescriptorClass` resolution into `ResolveOptions()` inside `UInteractionComponent`. But `UInteractionComponent` has no access to `UInteractionDescriptorSubsystem` (a `UGameInstanceSubsystem`) without a game instance reference — which components should not hold.

This creates an implicit dependency from the interaction host to the subsystem, or requires the subsystem to be passed as a parameter.

**Fix (applied in v2 spec):** Descriptor resolution is moved into `UInteractionManagerComponent::RunResolve()`, after `ResolveOptions()` writes the raw buffer. The subsystem is cached at `BeginPlay` on the manager, which has access to the game instance via `GetGameInstance()`. `UInteractionComponent::ResolveOptions` remains clean — it never touches the descriptor subsystem.

---

### 3. `IInteractionConditionProvider` references remain in `ResolveOptions` pseudocode

**Severity: Low**

The original `UInteractionComponent` resolution pseudocode still references `IInteractionConditionProvider::CanSeeInteraction`, even though the interface was explicitly removed in favor of `URequirement_Composite`. These are dead code references that would cause compile errors.

**Fix (applied in v2 spec):** All `IInteractionConditionProvider` references removed. Resolution calls `Config->EntryRequirements->Evaluate(Context)` only.

---

### 4. `ConditionIconOverride` removal is incomplete in `FResolvedInteractionOption`

**Severity: Low**

`UInteractionIconDataAsset` documentation references `FResolvedInteractionOption.ConditionIconOverride` as step 1 of the icon resolution chain (set by `IInteractionConditionProvider`). Since `IInteractionConditionProvider` is removed, `ConditionIconOverride` has no producer and should not be in the resolution chain at all.

**Fix (applied in v2 spec):** Icon resolution chain simplified to two steps: `EntryIconOverride` → `IconDataAsset->GetIconForState()` → null. `ConditionIconOverride` field removed from `FResolvedInteractionOption`.

---

### 5. `EInteractionRejectionReason` is missing a `RateLimited` value

**Severity: Low**

The network flow diagram in the original spec explicitly lists a rate limit step (0.3s cooldown per PC) as validation step [1] with rejection reason `RateLimited`. However, `EInteractionRejectionReason` has no `RateLimited` value — the enum only covers `OutOfRange`, `EntryNotFound`, `EntryUnavailable`, `TagMismatch`, `ConditionFailed`.

The server validation implementation also doesn't show where this rate limit is enforced or what state tracks it.

**Recommendation:** Either add `RateLimited` to the enum and add a `TMap<APlayerController*, float> LastInteractTime` to the server-side validation, or remove the rate limit from the network flow diagram and spec. For an MMORPG server, a per-connection rate limit is a worthwhile anti-spam measure and should be kept. Add it properly.

**Suggested addition to `UInteractionManagerComponent`:**
```cpp
// Server-side only — not replicated.
TMap<TWeakObjectPtr<APlayerController>, float> LastInteractTimestamps;

// In ServerRequestInteract_Implementation, before step [1]:
APlayerController* PC = Cast<APlayerController>(InstigatorPawn->GetController());
if (PC)
{
    float* LastTime = LastInteractTimestamps.Find(PC);
    const float Now = GetWorld()->GetTimeSeconds();
    if (LastTime && (Now - *LastTime) < 0.3f)
    { ClientRPC_OnInteractionRejected(EntryIndex, EInteractionRejectionReason::RateLimited); return; }
    LastInteractTimestamps.FindOrAdd(PC) = Now;
}
```
This map should be cleared when a PC disconnects — bind to `GameMode::Logout`.

---

### 6. `TriggerImmediateRescan` captures `GetWorld()` inside a lambda

**Severity: Low**

The original `TriggerImmediateRescan` implementation calls `GetWorld()` inside the timer lambda after capturing `WeakThis`. If `this` is valid, `GetWorld()` is also valid — so this is safe in practice. However, calling `Mgr->GetWorld()->GetTimerManager().SetTimer(...)` inside the lambda means the timer manager is accessed after the scan tick. If the lambda fires during world teardown and `GetWorld()` returns null, this will crash.

**Recommendation:** Check `GetWorld()` for null inside the lambda, or cache `GetWorld()->GetTimerManager()` before the lambda.

---

### 7. `FResolvedInteractionOption::Label` raw pointer exposes a footgun

**Severity: Low — by design, but worth noting**

The raw `const FText* Label` is documented as valid only for the frame. Widget authors who don't read the spec will cache this pointer and encounter intermittent crashes when the buffer is reset. The documented `BlueprintCallable` helper wrapper is the correct mitigation, but it would be worth adding a `#if WITH_EDITOR` assert in `GetOptionLabel()` that fires if the pointer is null — it's easy to silently dereference null in C++.

---

### 8. Scan scoring `TMap` allocations on every scan tick

**Severity: Low — optimization opportunity**

`ExecuteScan` creates two `TMap<AActor*, ...>` instances every scan tick. For `ScanPeriod = 0.2s` with 10 clients, that's 50 map allocations per second. These should be promoted to pre-allocated member containers (reset each scan, not re-created) like `OverlapBuffer`.

**Recommendation:**
```cpp
// In private members:
TMap<AActor*, float>                  ActorScoreCache;
TMap<AActor*, UInteractionComponent*> ActorComponentCache;

// In BeginPlay:
ActorScoreCache.Reserve(32);
ActorComponentCache.Reserve(32);

// In ExecuteScan (replace local declarations):
ActorScoreCache.Reset();
ActorComponentCache.Reset();
```

---

### 9. `UInteractionComponent::ResolveOptions` has a subtle authority inversion

**Severity: Medium**

In the original pseudocode, `ResolveOptions` evaluates `EntryRequirements` on the client to determine `Locked` state. The context sets `Context.Instigator = Cast<APawn>(SourceActor)`. But `SourceActor` is passed as the pawn from the manager's `GetOwner()` — on the client this is the locally controlled pawn, which is correct.

However, the `ResolveOptions` method is called on `UInteractionComponent`, which exists on the **interactable actor**. This means `GetWorld()` returns the world from the interactable's perspective — which on a dedicated server is the server world. If `ResolveOptions` is ever accidentally called server-side (e.g., in a debug utility or NPC AI), the requirement evaluation would run with a server-side context but client authority expectations — and the `ClientValidated` authority on `URequirementList` would silently pass or fail in unexpected ways.

**Mitigation (already in architecture):** `ResolveOptions` is only called from `UInteractionManagerComponent`, which guards behind `IsLocallyControlled()`. This is documented as a constraint and should be enforced with a `ensure(!HasAuthority())` check at the top of `ResolveOptions`.

---

## Suggestions

### S1. Consider a `RateLimitPeriod` property on `UInteractionManagerComponent`

Hardcoding 0.3s in the server validation is a magic constant. Expose it as `UPROPERTY(EditAnywhere, meta=(ClampMin=0.0, ClampMax=2.0)) float InteractRateLimitSeconds = 0.3f;` so designers can tune it per game type.

### S2. `UHighlightComponent::OwnedPrimitives` should respect LODs

For characters with many mesh components (armor pieces, accessories per skeleton bone), enabling Custom Depth on all primitives can be expensive. Consider a `bHighlightRootOnly` bool that limits `OwnedPrimitives` to the first mesh component found (or a designated LOD root). Profile before deciding.

### S3. Hold state machine cancellation on `DisablingTags` during Tick is frame-delayed

`IsDisabledByTag()` is checked at the start of `TickComponent` and also at the start of `ExecuteScan`. If a disabling tag is added between two scan ticks, the hold will not cancel until the next Tick (up to 0.016s later). For most cases this is imperceptible, but if immediate cancellation is required (e.g., stun prevents all interaction), consider having the tag component broadcast a delegate that `UInteractionManagerComponent` binds to for instant cancellation.

### S4. `FInteractionEntryNetState::EntryIndex` is redundant

The `EntryIndex` stored on each `FInteractionEntryNetState` item is redundant — the item's position in the `Items` array already encodes its index (since entries are appended in order at `BeginPlay`). `PostReplicatedChange` can derive the index from `Items.IndexOfByKey(this)` or by using the item's position. However, the `uint8 EntryIndex` field provides an explicit, crash-safe way to identify items in `PostReplicatedChange` without relying on array position — this is an acceptable tradeoff for clarity over micro-optimization. Keep it.
