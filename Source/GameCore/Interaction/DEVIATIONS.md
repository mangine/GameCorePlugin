# Interaction System — Implementation Deviations

## 1. `FRequirementContext` Fields (`Instigator`, `PlayerState`, `World`)

**Spec says:** The spec instructs creating `FRequirementContext` with `Context.Instigator`, `Context.PlayerState`, and `Context.World` fields when evaluating `EntryRequirements`.

**Actual:** The existing `FRequirementContext` struct (in `Requirements/RequirementContext.h`) only has a single `FInstancedStruct Data` field with no convenience pawn/world fields. These fields do not exist on the real type.

**Resolution:** In `InteractionComponent.cpp` (ResolveOptions) and `InteractionManagerComponent.cpp` (ServerRequestInteract), a default `FRequirementContext{}` is passed to `EntryRequirements->Evaluate`. Game module `URequirement` subclasses that need Instigator/PlayerState/World must:
- Retrieve the world via `GEngine->GetWorldFromContextObject` or store it in `Data` using a game-module-defined struct.
- The game module is responsible for packing any pawn/world context into `FRequirementContext::Data` before calling `EntryRequirements->Evaluate`.

**Impact:** Requirements with `Authority = ClientValidated` that depend on pawn context will not receive pawn data automatically. The interaction system passes an empty context — game module requirements must handle empty context gracefully or define a packing convention.

---

## 2. `URequirementLibrary::ValidateRequirements` Not Called at BeginPlay

**Spec says:** `URequirementLibrary::ValidateRequirements(Config->EntryRequirements->Requirements, ...)` should be called in `BeginPlay` to verify requirements are synchronous.

**Actual:** `URequirementLibrary` is not defined in the GameCore codebase (no `RequirementLibrary.h` found). The BeginPlay validation block uses an `ensureMsgf` to confirm requirements are non-null as a minimal substitute.

**Resolution:** When `URequirementLibrary` is implemented, replace the `#if !UE_BUILD_SHIPPING` block in `UInteractionComponent::BeginPlay` with the correct `URequirementLibrary::ValidateRequirements` call.

---

## 3. `ECC_GameTraceChannel_Interaction` Defined as Macro Fallback

**Spec says:** The scanner uses `ECC_GameTraceChannel_Interaction` as the sweep channel.

**Actual:** This channel is project-defined. Both `InteractionComponent.cpp` and `InteractionManagerComponent.cpp` define a fallback:
```cpp
#ifndef ECC_GameTraceChannel_Interaction
#define ECC_GameTraceChannel_Interaction ECC_GameTraceChannel1
#endif
```

**Resolution:** The project must define `ECC_GameTraceChannel_Interaction` in its engine config or a project header before including any Interaction System files. The fallback to `ECC_GameTraceChannel1` is for build compatibility only.

---

## 4. `InteractionNetState.cpp` Added (Not in Spec File List)

**Spec says:** Only `InteractionNetState.h` is listed in the file structure.

**Actual:** The `FFastArraySerializer` callbacks (`PostReplicatedAdd`, `PostReplicatedChange`, `PreReplicatedRemove`) require a `.cpp` file to avoid circular include issues and to provide the full `UInteractionComponent` type (needed to call `OnEntryStateChanged.Broadcast`). A `InteractionNetState.cpp` file was added.

**Impact:** None — additive only.

---

## 5. `UInteractionDescriptorSubsystem` Is `UGameInstanceSubsystem` (Not `UWorldSubsystem`)

**Spec says:** Architecture doc implies world lifetime. The class spec explicitly declares it as `UGameInstanceSubsystem`.

**Actual:** Implemented as `UGameInstanceSubsystem` per the class definition specification.

**Impact:** Descriptor cache survives level transitions. This is intentional per the spec.

---

## 6. `GetGameplayTags()` Fallback on Tag Filter Failure

**Spec says:** Tag pre-filter failure sets `State = Locked`.

**Actual:** If `SourceActor` or `TargetActor` does not implement `ITaggedInterface` but the config has required tags, the filter fails (sets Locked). This is correct behavior — a pawn without tags cannot satisfy tag requirements. No deviation.

---

## 7. `InteractionManagerComponent::BeginPlay` Uses `GetWorld()->GetGameInstance()`

**Spec says:** `GetGameInstance()->GetSubsystem<UInteractionDescriptorSubsystem>()`.

**Actual:** `GetWorld()->GetGameInstance()` used instead (equivalent but null-checked). Minor defensive coding addition, not a spec deviation.
