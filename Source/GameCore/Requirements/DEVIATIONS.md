# Requirement System — Implementation Deviations

## 1. `CollectWatchedEvents` — Iterative composite traversal (RequirementList.cpp)

**Spec:** The spec does not specify how composite children are recursed into during `CollectWatchedEvents`. The spec says "Collects all RequirementEvent.* tags from every requirement (composite children included)."

**Implementation:** An explicit iterative stack-based traversal was used to recurse into `URequirement_Composite::Children` at any nesting depth, rather than relying on `GetWatchedEvents` of the composite itself (which the spec does not define for `URequirement_Composite`). This ensures all leaf tags are collected regardless of composite nesting depth.

---

## 2. `GetAllRequirements` — Iterative stack traversal (RequirementList.cpp)

**Spec:** The spec declares the method signature but does not prescribe the traversal algorithm.

**Implementation:** An explicit iterative stack-based traversal was used to collect all requirements recursively from composite children. This avoids stack overflow on deeply nested composites.

---

## 3. `ValidateRequirements` — Authority mismatch check is a heuristic placeholder (RequirementLibrary.cpp)

**Spec:** "If ListAuthority is ClientOnly or ClientValidated: logs a warning if any requirement declares server-only data access (heuristic check)."

**Implementation:** The server-only data access heuristic is implemented as a `Verbose` log advisory rather than per-requirement introspection, because `URequirement` does not define an `IsServerOnly()` virtual method. Concrete requirement subclasses would need to opt-in via a future virtual to enable per-requirement authority checking. This matches the spec's description of it as a "heuristic check."

---

## 4. `URequirementLibrary` — Not a `UObject` subclass

**Spec:** The spec says "plain `UObject` subclass with static methods" in the Architecture overview, but also says "NOT a `UBlueprintFunctionLibrary`. It is a plain C++ helper class (`static` methods)."

**Implementation:** `URequirementLibrary` is implemented as a plain C++ class (no `UObject` inheritance, no `UCLASS` macro, `= delete` constructor). The Architecture spec text saying "plain `UObject` subclass" appears to be an editorial inconsistency — the URequirementLibrary spec itself explicitly states "plain C++ class (`static` methods)" and "Not a `UBlueprintFunctionLibrary`." A plain C++ class is the correct interpretation.

---

## 5. `GetDescription` — `WITH_EDITOR` implementation added to `URequirement_Composite` (RequirementComposite.cpp)

**Spec:** The spec declares `GetDescription()` on `URequirement_Composite` under `#if WITH_EDITOR` but does not provide its implementation body.

**Implementation:** A minimal implementation returning `"Composite [AND|OR|NOT] (N children)"` was written, consistent with the base class pattern described in `URequirement.md`.

---

## 6. `RequirementList.h` — Forward declarations instead of full includes for EventBus types

**Spec:** "forward-declare or include `EventBus/GameCoreEventWatcher.h`"

**Implementation:** Forward declarations for `UGameCoreEventWatcher`, `FEventWatchHandle`, and `EGameCoreEventScope` are used in the header to avoid pulling in Event Bus headers into every file that includes `RequirementList.h`. The full include of `EventBus/GameCoreEventWatcher.h` is in `RequirementList.cpp` only. This is the preferred zero-dependency approach consistent with the Architecture spec.

Note: `FEventWatchHandle` and `EGameCoreEventScope` are defined in `EventBus/GameCoreEventWatcher.h` and `EventBus/GameCoreEventBus.h` respectively — those files must exist (EventBus System must be implemented) for this code to compile.
