# Requirement Sets

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

`URequirementList` is the sole concrete requirement set class. It is a `UPrimaryDataAsset` that owns an array of requirements and evaluates them as a unit using a configurable operator (AND or OR). Consuming systems hold a `TObjectPtr<URequirementList>` asset reference. Because it is a Data Asset, a single asset can be referenced by any number of consuming systems with zero duplication — the canonical pattern for shared prerequisites such as ore nodes, crafting recipes, and quest gates.

`URequirementSet` has been removed. `URequirementList` directly inherits `UPrimaryDataAsset` and owns the full evaluation interface. There is no abstract base — if a second set type is ever needed, introduce a shared base at that time.

**File:** `Requirements/RequirementList.h / .cpp`

---

# `URequirementList`

```cpp
// Concrete requirement set asset. Create one asset per unique requirement configuration.
// Multiple systems (ore nodes, recipes, interactions) may reference the same asset.
//
// Operator controls top-level evaluation:
//   AND — all requirements must pass (default, most common).
//   OR  — any single requirement passing is sufficient.
//
// Any boolean expression is achievable by nesting URequirement_Composite children
// within the Requirements array. For example, (A AND B) OR (C AND D):
//   Operator = OR
//   Requirements[0] = URequirement_Composite(AND) { A, B }
//   Requirements[1] = URequirement_Composite(AND) { C, D }
UCLASS(BlueprintType, DisplayName = "Requirement List")
class GAMECORE_API URequirementList : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    // Top-level evaluation operator.
    // AND: all requirements must pass. Short-circuits on first failure.
    // OR:  any requirement passing is sufficient. Short-circuits on first pass.
    // Array order is evaluation order — place cheap checks first.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirements")
    ERequirementListOperator Operator = ERequirementListOperator::AND;

    // The requirements evaluated by this list.
    // URequirement_Composite is a valid element — use it for nested AND/OR/NOT logic.
    UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = "Requirements")
    TArray<TObjectPtr<URequirement>> Requirements;

    // Network authority for this asset. Declared once by the designer.
    // Consuming systems do not pass or override authority at call sites.
    // If two systems need different authority for the same conditions, use two assets.
    // Defined in RequirementList.h alongside URequirementList.
    UPROPERTY(EditDefaultsOnly, Category = "Network")
    ERequirementEvalAuthority Authority = ERequirementEvalAuthority::ServerOnly;

    // ── Evaluation — primary public API ──────────────────────────────────────
    // Consuming systems call these directly. Never call URequirementLibrary externally.

    UFUNCTION(BlueprintCallable, Category = "Requirements")
    FRequirementResult Evaluate(const FRequirementContext& Context) const;

    bool IsAsync() const;

    void EvaluateAsync(const FRequirementContext& Context,
                       TFunction<void(FRequirementResult)> OnComplete) const;

    // Called by URequirementWatcherComponent at RegisterSet time.
    void CollectWatchedEvents(FGameplayTagContainer& OutEvents) const;

    // Returns all requirements flat (for cache array sizing in the Watcher System).
    TArray<URequirement*> GetAllRequirements() const;
};
```

> **Authoring rule.** One `URequirement` subclass per behaviour — vary configuration via properties, not subclasses. Never create a separate Blueprint subclass of a requirement just to hardcode a different item tag or level threshold. Those are properties on the requirement instance inside the asset.
> 

`Evaluate` and `EvaluateAsync` use `URequirementLibrary` internally as a helper. `URequirementLibrary` is not a public API for consuming systems — always call `List->Evaluate(Context)`.

---

# `ERequirementListOperator`

Defined in `RequirementList.h`.

```cpp
UENUM(BlueprintType)
enum class ERequirementListOperator : uint8
{
    // All requirements must pass. Short-circuits on first failure.
    // Use for prerequisite gates: level AND tool equipped AND quest complete.
    AND UMETA(DisplayName = "All Must Pass (AND)"),

    // Any single requirement passing is sufficient. Short-circuits on first pass.
    // Use for alternative unlock paths: guild member OR reputation threshold.
    OR  UMETA(DisplayName = "Any Must Pass (OR)"),
};
```

---

# `ERequirementEvalAuthority`

Defined in `RequirementList.h` alongside `URequirementList`. Authority is declared on the asset by the designer — not passed by call sites. `RegisterSet` reads `List->Authority` directly. If two systems need different authority for the same logical conditions, they reference two separate assets.

> **Design rule.** Never add an authority override parameter to `RegisterSet`. If you feel the need to override, create a second asset.
> 

```cpp
UENUM(BlueprintType)
enum class ERequirementEvalAuthority : uint8
{
    // Server evaluates only. Client never receives result until server decides.
    // Use for all gameplay-gating checks (loot, quest progression, ability use).
    ServerOnly UMETA(DisplayName = "Server Only"),

    // Client evaluates only. Server never checks.
    // Use for purely cosmetic or UI-gating checks (show tooltip, grey-out button).
    // All requirements in a ClientOnly list must return ClientOnly or Both from
    // GetDataAuthority(). A ServerOnly requirement here cannot be evaluated on the
    // client — ValidateRequirements treats this as a design error at BeginPlay.
    ClientOnly UMETA(DisplayName = "Client Only"),

    // Client evaluates for responsiveness. On all-pass, client fires a Server RPC.
    // Server re-evaluates fully from its own context — never trusts the client result.
    // Use for player-facing unlocks where immediate UI feedback matters (quest available).
    //
    // IMPORTANT: All requirements in a ClientValidated list must return ClientOnly or
    // Both from GetDataAuthority(). A ServerOnly requirement cannot be evaluated on
    // the client — it would silently optimistic-pass, fire the RPC, and be rejected
    // by the server with no predictable client-side signal. If any requirement needs
    // server-only data, use a ServerOnly list instead.
    // ValidateRequirements enforces this constraint at BeginPlay in development builds.
    ClientValidated UMETA(DisplayName = "Client Validated"),
};
```

**Security note on `ClientValidated`.** The server RPC must trigger a full server-side re-evaluation using a server-constructed `FRequirementContext`. The client result is a hint only — it cannot bypass the server check.

---

# `FRequirementSetRuntime` — Per-Player Cache

The Watcher System maintains one `FRequirementSetRuntime` per registered list per player. This struct holds the per-player evaluation cache — it is never part of the shared `URequirementList` asset.

```cpp
// Uniquely identifies a registered requirement list within a player's watcher component.
// Issued by URequirementWatcherComponent::RegisterSet.
struct FRequirementSetHandle
{
    uint32 Id = 0;
    bool IsValid() const { return Id != 0; }
};

// Per-player runtime state for one registered URequirementList.
// Lives in URequirementWatcherComponent. Never replicated.
USTRUCT()
struct FRequirementSetRuntime
{
    GENERATED_BODY()

    // The shared list asset. Not per-player — many players may reference the same asset.
    UPROPERTY()
    TObjectPtr<URequirementList> Asset;

    // Parallel to Asset->GetAllRequirements().
    // Indexed by requirement position in the flat list.
    // CachedTrue entries for monotonic requirements are never re-evaluated.
    TArray<ERequirementCacheState> CachedResults;

    // Authority read from Asset->Authority at registration time.
    ERequirementEvalAuthority Authority = ERequirementEvalAuthority::ServerOnly;

    // Unique handle for this registration.
    FRequirementSetHandle Handle;
};

// Three-state cache entry.
UENUM()
enum class ERequirementCacheState : uint8
{
    Uncached,     // Not yet evaluated.
    CachedFalse,  // Last result was Fail. Will be re-evaluated on next dirty flush.
    CachedTrue,   // Last result was Pass. If bIsMonotonic, this is permanent.
};
```

**Memory note.** Cache arrays are sized to `GetAllRequirements().Num()` at registration time. With average 5 requirements per set and a reasonable number of active sets per player, total memory per player is in the low kilobytes — negligible.

---

# Consuming Systems — Integration Pattern

```cpp
// In a Data Asset (e.g. UOreNodeDefinition, UQuestDefinition):
// No Instanced specifier — URequirementList is a UPrimaryDataAsset referenced by pointer.
UPROPERTY(EditDefaultsOnly, Category = "Requirements")
TObjectPtr<URequirementList> Requirements;

// One-shot server-side check (e.g. confirming a mine action RPC):
FRequirementContext Ctx;
Ctx.PlayerState = PS;
Ctx.World       = GetWorld();
Ctx.Instigator  = GetPawn();
FRequirementResult Result = OreNodeDef->Requirements->Evaluate(Ctx);

// Watched registration — authority is read from List->Authority, no parameter needed:
URequirementWatcherComponent* Watcher = PS->GetComponentByClass<URequirementWatcherComponent>();
FRequirementSetHandle Handle = Watcher->RegisterSet(
    QuestDef->Requirements,
    FOnRequirementSetDirty::CreateUObject(this, &UQuestComponent::OnRequirementsDirty)
);
```

Store the `FRequirementSetHandle`. Call `Watcher->UnregisterSet(Handle)` when the system no longer needs to track this list to prevent leaking subscriptions.

---

# Known Limitations

- **`CollectWatchedEvents` on deep expression trees has upfront cost.** Called once at `RegisterSet` time — not per-frame. Acceptable for sets with up to ~50 nodes; pathological trees with hundreds of nested composites should be restructured.
- **Unregistering sets is the caller's responsibility.** The watcher component does not track set lifetime. Leaking registrations wastes subscription slots and keeps stale cache entries alive.
