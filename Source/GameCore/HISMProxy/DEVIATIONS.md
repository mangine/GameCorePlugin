# HISM Proxy Actor System — Runtime Deviations

## Files covered
`Source/GameCore/HISMProxy/`

---

## DEV-R-1: Log category declared as static per-file rather than a shared extern

**Spec says:** `UE_LOG(LogGameCore, ...)` throughout.
**Implementation:** Each `.cpp` file declares `DEFINE_LOG_CATEGORY_STATIC(LogGameCore, Log, All)` locally.
**Reason:** No shared `LogGameCore` category header was present in the existing codebase. The project pattern (see `PersistenceRegistrationComponent.cpp`) uses `DEFINE_LOG_CATEGORY_STATIC` per file. Using a static avoids polluting the global log category namespace and does not require creating a new shared header. All log output is functionally identical. If a shared `LogGameCore` is introduced later, replace the static declarations with `DECLARE_LOG_CATEGORY_EXTERN` / `DEFINE_LOG_CATEGORY` in the appropriate module header/source.

---

## DEV-R-2: `AHISMProxyHostActor` constructor creates a `USceneComponent` root

**Spec says:** No explicit mention of root component creation.
**Implementation:** Constructor creates a `USceneComponent` named `"Root"` as the root component.
**Reason:** `CreateComponentsForEntry` attaches the auto-created HISM component via `AttachToComponent(GetRootComponent(), ...)`. `AActor::GetRootComponent()` returns `nullptr` by default when no components are added in the constructor, which would cause a null dereference at attachment. A plain `USceneComponent` root is the minimal, standard UE solution.

---

## DEV-R-3: `UHISMProxyConfig::IsDataValid` implemented inline in the header

**Spec says:** Header declares `IsDataValid`; spec shows a separate code block for the implementation.
**Implementation:** Implementation is placed inline (guarded by `#if WITH_EDITOR`) in `HISMProxyConfig.h`, consistent with the spec's declaration that this class is "header-only, no .cpp needed."
**Reason:** The spec explicitly states no `.cpp` is needed for `UHISMProxyConfig`. The inline implementation satisfies the spec's intent without introducing an unnecessary source file.

---

## DEV-R-4: `HISMProxyBridgeComponent.cpp` includes `TimerManager.h` explicitly

**Spec says:** No explicit include list.
**Implementation:** `#include "TimerManager.h"` added.
**Reason:** `GetWorld()->GetTimerManager()` requires `FTimerManager` to be complete. Some engine PCH configurations include it transitively; explicit inclusion is required for correctness with `UseExplicitOrSharedPCHs`.

---

## No other deviations. All logic, field names, method signatures, enum values, delegate types, tag strings, custom data slot assignments, pool spawn location formula, and scratch-buffer reset patterns match the specification exactly.
