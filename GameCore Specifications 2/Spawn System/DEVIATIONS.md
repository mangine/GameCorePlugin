# Spawn System — Implementation Deviations

## DEV-1: FRequirementContext has no World/Instigator fields

**Spec shows:**
```cpp
FRequirementContext Ctx;
Ctx.World      = GetWorld();
Ctx.Instigator = GetOwner();
```

**What we implemented:**
```cpp
FRequirementContext Ctx; // Empty context — spawn requirements are world-state only.
if (!Entry.SpawnRequirements->Evaluate(Ctx).bPassed) continue;
```

**Reason:** `FRequirementContext` in the GameCore plugin uses `FInstancedStruct Data` only (no `World` or `Instigator` fields). This is by design — requirements that need world context perform subsystem lookups directly. Spawn requirements that need world/anchor data should use `FRequirementContext::Make(FSpawnEvaluationContext{...})` where `FSpawnEvaluationContext` is a game-module-defined struct. This is consistent with how `FQuestEvaluationContext` works. Passing an empty context is correct for requirements that query world state via subsystems without needing player or instigator context.

---

## DEV-2: File placed in Spawning/ not Spawn/

**Spec says:** `Source/GameCore/Spawning/` (Architecture.md file structure section).

**What we implemented:** Files placed in `Source/GameCore/Spawning/` as specified. No deviation.

---

## DEV-3: No URequirementLibrary::ValidateRequirements call in BeginPlay

**Spec shows:** Non-shipping validation call `URequirementLibrary::ValidateRequirements(Entry.SpawnRequirements, ...)` in BeginPlay.

**What we implemented:** Removed this call. `URequirementLibrary` is not yet defined in the plugin, and its validation signature is not specified in the available specs. The `#if !UE_BUILD_SHIPPING` guard is present as a comment stub.

**Reason:** `URequirementLibrary` is a utility type not implemented as part of this spec set. The validation intent is preserved in the Architecture doc.
