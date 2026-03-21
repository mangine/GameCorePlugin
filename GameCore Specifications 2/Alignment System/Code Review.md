# Alignment System — Code Review

---

## Overview

The Alignment System is well-scoped and architecturally clean. The core design — tag-driven axes, hysteresis buffer, batch mutations, server-authoritative replication — is solid and follows GameCore's established patterns. The spec is consistent in intent. The issues below are bugs, inconsistencies, and quality gaps found during the migration review.

---

## Issues Found & Fixed in Migration

### 1. `GetEffectiveAlignment` Returns 0 on Clients (Bug)

**Severity: High**

**Original spec:** `GetEffectiveAlignment` calls `Definitions.FindRef(Tag)`, which returns `nullptr` on clients (the `Definitions` map is server-only). The comment acknowledged this: *"On clients, Def will be null. Callers should cache the definition asset directly."*

This is a broken contract. A `BlueprintPure` function that silently returns 0 on the client for every query will cause subtle UI bugs that are hard to diagnose.

**Fix applied:** `EffectiveMin` and `EffectiveMax` are now stored directly on `FAlignmentEntry` and set at `RegisterAlignment` time. Since `FAlignmentEntry` is replicated via `FFastArraySerializer`, clients have the clamp range available. `GetEffectiveAlignment` now calls `Entry->GetEffectiveValue()` which uses the cached range — no definition lookup needed.

---

### 2. `FAlignmentSaveEntry` Is an Orphan Type (Bug)

**Severity: High**

**Original spec:** The component header declares `LoadFromSave(const TArray<FAlignmentSaveEntry>&)` and `GetSaveEntries(TArray<FAlignmentSaveEntry>&)`, but `FAlignmentSaveEntry` is never defined anywhere in the spec. No struct, no file, no mention beyond the function signature.

**Fix applied:** Removed the custom `LoadFromSave`/`GetSaveEntries` API entirely. Persistence is handled by implementing `IPersistableComponent` (`Serialize_Save` / `Serialize_Load`), which is the established pattern for all GameCore components. This eliminates the orphan type and removes a redundant persistence surface.

---

### 3. `FAlignmentChangedMessage::PlayerState` Uses `TObjectPtr` (Bug)

**Severity: Medium**

**Original spec:** `FAlignmentChangedMessage` declares `TObjectPtr<APlayerState> PlayerState`. Per the v2 message authoring rules: *"Use `TWeakObjectPtr` for all actor and component references — message structs are value types that may outlive the broadcast frame if stored by a listener."*

**Fix applied:** Changed to `TWeakObjectPtr<APlayerState>`.

---

### 4. `COND_OwnerOnly` Not Applied in Replication Code (Bug)

**Severity: Medium**

**Original spec:** The `Important Notes` section states *"`AlignmentData` is replicated to the owning client only (condition `COND_OwnerOnly` recommended)"*, but the actual `GetLifetimeReplicatedProps` implementation uses `DOREPLIFETIME` with no condition.

**Fix applied:** Changed to `DOREPLIFETIME_CONDITION(UAlignmentComponent, AlignmentData, COND_OwnerOnly)`.

---

### 5. Event Bus API Inconsistency (Cleanup)

**Severity: Low**

**Original spec:** `GMS Event Messages.md` uses `UGameCoreEventBus2::Get(this)` in the listener example while the main component implementation uses `UGameCoreEventSubsystem::Get(this)`. Neither matches the v2 canonical class name `UGameCoreEventBus`.

**Fix applied:** All references updated to `UGameCoreEventBus::Get(this)`.

---

### 6. `FAlignmentArray::FindByTag` Not in Sync Between Files

**Severity: Low**

**Original spec:** `FAlignmentArray` is defined in `Runtime Types.md`, but `UAlignmentComponent` calls `AlignmentData.FindByTag(...)`. This is fine structurally but the method was only documented in one place. Now consolidated in `AlignmentTypes.md`.

---

## Design Suggestions

### A. Initial Value Support

Currently, every axis always starts at `UnderlyingValue = 0`. Games may need an axis to start at a non-neutral position (e.g. a new character is slightly lawful by default). Adding an optional `float InitialValue = 0.f` parameter to `RegisterAlignment` would be a low-cost, high-value improvement.

---

### B. `FAlignmentChangedMessage` Does Not Carry Instigator

The v2 message authoring rule states: *"First field is always subject/instigator context."* The current struct carries `PlayerState` (the subject) but not an instigator. For systems that need to know *who* triggered the alignment change (e.g. a quest that awards points only when a specific NPC causes the change), the `FRequirementContext` instigator is discarded after the requirements check.

Consider adding `TWeakObjectPtr<AActor> Instigator` to `FAlignmentChangedMessage`, populated from `FRequirementContext::Subject` at the call site.

---

### C. `Migrate()` Not Overridden

The current schema is version 1 with a simple tag+float layout. No migration is needed now, but the interface's `Migrate()` default no-op will silently succeed if a future schema change is deployed without the override. Add a `static_assert` or a dev-build check in `Serialize_Load` that asserts `SavedVersion == GetSchemaVersion()` until a migration is actually needed — this makes future schema changes impossible to accidentally ship without the migration handler.

---

### D. No Dev-Build Duplicate Tag Validation

Registering two `UAlignmentDefinition` assets with the same `AlignmentTag` on the same component silently degrades: the second `RegisterAlignment` call returns early (idempotent), meaning the second definition's requirements and ranges are silently ignored. This is a designer error that is extremely hard to diagnose in production.

Add a dev-build check in `RegisterAlignment`:
```cpp
#if !UE_BUILD_SHIPPING
if (Definitions.Contains(Tag))
{
    const UAlignmentDefinition* ExistingDef = Definitions.FindRef(Tag);
    if (ExistingDef != Definition)
    {
        UE_LOG(LogAlignment, Error,
            TEXT("RegisterAlignment: two different definitions registered for tag '%s' on %s. "
                 "Second registration ignored. This is a content authoring error."),
            *Tag.ToString(), *GetOwner()->GetName());
    }
    return;
}
#endif
```

---

### E. `ApplyAlignmentDeltas` Has No Blueprint-Accessible Single-Axis Variant

The batch API is correct by design. However, Blueprint designers will find it cumbersome to construct a `TArray<FAlignmentDelta>` for a single-axis change. A thin Blueprint wrapper is worth adding:

```cpp
UFUNCTION(BlueprintCallable, Category = "Alignment", BlueprintAuthorityOnly,
          meta=(DisplayName="Apply Single Alignment Delta"))
void ApplySingleAlignmentDelta(
    FGameplayTag AlignmentTag,
    float Delta,
    const FRequirementContext& Context)
{
    ApplyAlignmentDeltas({{ AlignmentTag, Delta }}, Context);
}
```

This is purely syntactic sugar; it calls the main path with no extra overhead.
