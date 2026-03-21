# Requirement System — Enums

**File:** `Requirements/RequirementList.h`

---

## `ERequirementEvalAuthority`

Declared once on the `URequirementList` asset. Controls which network side is permitted to evaluate the list. `RegisterWatch` reads this and maps it to `EGameCoreEventScope` so the watcher enforces it — closures are silently skipped on the wrong side.

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
    // Server re-evaluates fully from its own context — never trusts the client result.
    // Use for player-facing unlocks where immediate UI feedback matters.
    ClientValidated UMETA(DisplayName = "Client Validated"),
};
```

> **Security rule.** In `ClientValidated` mode the Server RPC handler **must** re-evaluate using a server-built `FRequirementContext`. The client result is a UX hint only — it never controls the action.

---

## `ERequirementListOperator`

Top-level combination operator on `URequirementList`.

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

## `ERequirementOperator`

**File:** `Requirements/RequirementComposite.h`

Operator for `URequirement_Composite`. Adds NOT on top of `ERequirementListOperator`.

```cpp
UENUM(BlueprintType)
enum class ERequirementOperator : uint8
{
    // All children must pass. Short-circuits on first failure.
    AND UMETA(DisplayName = "AND — All Must Pass"),

    // Any child passing is sufficient. Short-circuits on first pass.
    OR  UMETA(DisplayName = "OR — Any Must Pass"),

    // Exactly one child. Result is inverted.
    // Child passes → Composite fails (using NotFailureReason).
    // Child fails  → Composite passes.
    NOT UMETA(DisplayName = "NOT — Must Not Pass"),
};
```
