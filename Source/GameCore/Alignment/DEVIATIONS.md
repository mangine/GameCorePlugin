# Alignment System — Implementation Deviations

## Overview
The Alignment System implementation follows the spec closely. The deviations below are minor adaptations required for compilation against the existing codebase.

---

## Deviation 1: `IPersistableComponent::IsDirty()` Added

**Spec:** Does not mention `IsDirty()` in the `IPersistableComponent` section for `UAlignmentComponent`.

**Actual:** `PersistableComponent.h` declares `virtual bool IsDirty() const = 0;` as a pure virtual. `UAlignmentComponent` implements it as `return bDirty;`.

**Impact:** None — fully satisfies the interface contract and adds no behavioral change.

---

## Deviation 2: `GameCoreEventTags::Alignment_Changed` Registered in `GameCoreEventTags.cpp`

**Spec:** Says to register the tag in `FGameCoreModule::StartupModule()` in `GameCore.cpp`.

**Actual:** Registered via a `static FGameCoreEventTagsRegistrar` struct in `GameCoreEventTags.cpp`, consistent with the existing pattern used by `Progression_*` and `StateMachine_*` tags in the same file. `GameCore.cpp` explicitly delegates to per-system `.cpp` files.

**Impact:** Functionally identical — static initializer fires at module load. Consistent with codebase pattern.

---

## Deviation 3: `AlignmentComponent.h` Includes `GameFramework/PlayerState.h`

**Spec:** Spec header does not explicitly list this include.

**Actual:** Added because `FAlignmentChangedMessage` declares `UPROPERTY() TWeakObjectPtr<APlayerState> PlayerState;`, which requires the full `APlayerState` type for UHT property registration.

**Impact:** Slightly broader include; no behavioral change.

---

## Deviation 4: `RequirementContext.h` Included in `AlignmentComponent.h`

**Spec:** Spec header shows `ApplyAlignmentDeltas` taking `const FRequirementContext& Context` but does not list `RequirementContext.h` as an explicit include in the header.

**Actual:** Added `#include "Requirements/RequirementContext.h"` to `AlignmentComponent.h` since the full type is required at the function declaration site.

**Impact:** None — required for compilation.

---

## Deviation 5: Event Tag String Uses `GameCoreEvent.Alignment.Changed`

**Spec:** `AlignmentEventTags.md` shows the tag as `GameCoreEvent.Alignment.Changed`.

**Actual:** Registered as `TEXT("GameCoreEvent.Alignment.Changed")` in `GameCoreEventTags.cpp`. Consistent with spec.

**Impact:** None.

---

## Non-Deviations (Confirmed Spec-Compliant)

- `FFastArraySerializer` with `COND_OwnerOnly` replication implemented as specified.
- `EffectiveMin/Max` cached on `FAlignmentEntry` at `RegisterAlignment` time for client queries.
- `Definitions` map is server-only (not replicated, populated by `RegisterAlignment`).
- `ApplyAlignmentDeltas` is batch-only; GMS broadcast fires once per batch.
- `IPersistableComponent` persistence saves `UnderlyingValue` only; range re-applied at `RegisterAlignment`.
- `Serialize_Load` silently skips unknown tags (axis removed from game).
- `NotifyDirty` is a no-op when no `UPersistenceRegistrationComponent` is present.
- `UAlignmentDefinition` `IsDataValid` validates all four range constraints.
- `FAlignmentChangedMessage` message struct lives in `AlignmentComponent.h` per v2 convention.
