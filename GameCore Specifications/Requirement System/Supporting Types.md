# Supporting Types

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

All plain structs and enums used across the system. No types here have outgoing dependencies on other GameCore modules.

**File:** `Requirements/RequirementContext.h`

---

# `FRequirementContext`

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

    // Convenience constructor.
    template<typename T>
    static FRequirementContext Make(const T& InData)
    {
        FRequirementContext Ctx;
        Ctx.Data.InitializeAs<T>(InData);
        return Ctx;
    }
};
```

**Rules:**
- The caller is responsible for putting the right struct type into `Data`.
- A requirement that needs world state and has no event data (e.g. `URequirement_MinLevel` in an imperative check) receives a context whose `Data` contains a caller-defined snapshot struct with a `PlayerState` pointer or equivalent.
- A requirement that only responds to events (e.g. a level-up event requirement) may leave imperative `Evaluate` returning `Fail` by default and only implement `EvaluateFromEvent`.
- Never add typed fields directly to `FRequirementContext`. All domain data lives inside `Data`.

---

# `FRequirementResult`

Return type of every evaluation call.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FRequirementResult
{
    GENERATED_BODY()

    UPROPERTY() bool bPassed = false;

    // Optional player-facing reason shown in UI when the check fails.
    // Localisation-ready. Empty on Pass results.
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

**Rules:**
- Always construct via `Pass()` / `Fail()`. Never set `bPassed` directly.
- `FailureReason` uses `FText` for localisation. Never `FString`.
- Consuming systems forward `FailureReason` to the client via a targeted RPC.

---

# `ERequirementEvalAuthority`

**File:** `Requirements/RequirementList.h`

Declared once on the `URequirementList` asset. Controls which network side evaluates the list. The watcher manager enforces this — lists are silently skipped on the wrong side.

```cpp
UENUM(BlueprintType)
enum class ERequirementEvalAuthority : uint8
{
    // Server evaluates only. Use for all gameplay-gating checks.
    // Client never receives a result until the server decides.
    ServerOnly UMETA(DisplayName = "Server Only"),

    // Client evaluates only. Server never checks.
    // Use for cosmetic / UI gating where authoritative evaluation is not needed.
    ClientOnly UMETA(DisplayName = "Client Only"),

    // Client evaluates for responsiveness. On all-pass, fires a Server RPC.
    // Server re-evaluates fully from its own context — never trusts the client.
    // Use for player-facing unlocks where immediate UI feedback matters.
    ClientValidated UMETA(DisplayName = "Client Validated"),
};
```

> **Security rule.** In `ClientValidated` mode the Server RPC handler must re-evaluate using a server-built `FRequirementContext`. The client result is a UX hint only.

---

# `ERequirementListOperator`

**File:** `Requirements/RequirementList.h`

```cpp
UENUM(BlueprintType)
enum class ERequirementListOperator : uint8
{
    // All requirements must pass. Short-circuits on first failure.
    AND UMETA(DisplayName = "All Must Pass (AND)"),

    // Any single requirement passing is sufficient. Short-circuits on first pass.
    OR  UMETA(DisplayName = "Any Must Pass (OR)"),
};
```

---

# Common Context Structs (Conventions)

These structs are not part of `Requirements/` — they live in their owning modules. They are documented here as a convention reference for how consuming systems build `FRequirementContext`.

Each module defines a snapshot struct that captures all data its requirement subclasses need. This struct is passed to `FRequirementContext::Make<T>()` at evaluation time.

```cpp
// Example — defined in Leveling module:
USTRUCT()
struct FLevelingContext
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<APlayerState> PlayerState = nullptr;
    UPROPERTY() int32 CurrentLevel = 0;
};

// Example — defined in a generic player query:
USTRUCT()
struct FPlayerContext
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<APlayerState> PlayerState = nullptr;
    UPROPERTY() TObjectPtr<UWorld> World = nullptr;
};

// Example — level-up event payload (also usable as FRequirementContext data):
USTRUCT()
struct FLevelChangedEvent
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<APlayerState> PlayerState = nullptr;
    UPROPERTY() int32 OldLevel = 0;
    UPROPERTY() int32 NewLevel = 0;
};
```

Requirement subclasses that accept multiple context types check `Data.GetScriptStruct()` and handle each type they support.
