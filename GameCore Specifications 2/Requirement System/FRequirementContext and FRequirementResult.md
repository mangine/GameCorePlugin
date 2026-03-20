# `FRequirementContext` and `FRequirementResult`

**File:** `Requirements/RequirementContext.h`

All plain structs used as the evaluation contract. No outgoing dependencies on other GameCore modules.

---

## `FRequirementContext`

The single evaluation input passed to every `Evaluate` and `EvaluateFromEvent` call. Carries an `FInstancedStruct` so any struct type can be the context — player state snapshots, event payloads, interaction queries, anything.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FRequirementContext
{
    GENERATED_BODY()

    // The evaluation data. The requirement subclass declares what struct type
    // it expects and casts accordingly. May be empty for requirements that
    // derive all data from world state via subsystem lookup.
    UPROPERTY(BlueprintReadWrite)
    FInstancedStruct Data;

    // Convenience factory. Initialises Data as type T.
    template<typename T>
    static FRequirementContext Make(const T& InData)
    {
        FRequirementContext Ctx;
        Ctx.Data.InitializeAs<T>(InData);
        return Ctx;
    }
};
```

**Important notes:**
- The caller is responsible for putting the correct struct type into `Data`.
- A requirement that needs world state and has no event data receives a context whose `Data` contains a caller-defined snapshot struct (e.g. `FLevelingContext`) with a `PlayerState` pointer or equivalent.
- A requirement that only responds to events implements `EvaluateFromEvent` and may leave `Evaluate` returning `Fail` by default.
- **Never add typed fields directly to `FRequirementContext`.** All domain data lives inside `Data`. Adding fields would create import pressure in the zero-dependency base module.
- Event payloads from `UGameCoreEventBus` arrive as `FInstancedStruct` and can be assigned directly to `Ctx.Data` without any translation.

---

## `FRequirementResult`

Return type of every evaluation call.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FRequirementResult
{
    GENERATED_BODY()

    UPROPERTY() bool bPassed = false;

    // Optional player-facing reason shown in UI when the check fails.
    // Localisation-ready via FText. Empty on Pass results.
    UPROPERTY() FText FailureReason;

    static FRequirementResult Pass()
    {
        return { true, FText::GetEmpty() };
    }

    static FRequirementResult Fail(FText Reason = {})
    {
        return { false, Reason };
    }
};
```

**Important notes:**
- Always construct via `Pass()` / `Fail()`. Never set `bPassed` directly.
- `FailureReason` uses `FText` for localisation. Never `FString`.
- Consuming systems forward `FailureReason` to the client via a targeted RPC. It is never sent by the requirement system itself.

---

## Context Struct Convention

Context structs are not part of `Requirements/` — they live in their owning modules. Each module defines a snapshot struct capturing all data its requirement subclasses need.

Requirement subclasses that accept multiple context types check `Context.Data.GetScriptStruct()` or use `GetPtr<T>()` for each supported type:

```cpp
// A leveling snapshot struct — lives in Leveling/LevelingTypes.h
USTRUCT()
struct FLevelingContext
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<APlayerState> PlayerState = nullptr;
    UPROPERTY() int32 CurrentLevel = 0;
};

// A level-up event payload — also usable directly as FRequirementContext data.
USTRUCT()
struct FLevelChangedEvent
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<APlayerState> PlayerState = nullptr;
    UPROPERTY() int32 OldLevel = 0;
    UPROPERTY() int32 NewLevel = 0;
};
```

Requirements that accept both types:
```cpp
if (const FLevelingContext* Snap = Context.Data.GetPtr<FLevelingContext>())
    CurrentLevel = Snap->CurrentLevel;
else if (const FLevelChangedEvent* Evt = Context.Data.GetPtr<FLevelChangedEvent>())
    CurrentLevel = Evt->NewLevel;
else
    return FRequirementResult::Fail(LOCTEXT("BadCtx", "Unexpected context type."));
```
