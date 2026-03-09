# Requirement Sets

**Sub-page of:** [Requirement System](../Requirement%20System%20318d261a36cf8170a13ff15cbade3f20.md)

Requirement Sets are the grouping layer between individual `URequirement` instances and the consuming systems that use them. A set is a `UObject` that owns an array of requirements and knows how to evaluate them as a unit. Consuming systems hold a `TObjectPtr<URequirementSet>` (marked `Instanced`) rather than a raw `TArray<TObjectPtr<URequirement>>`.

**File:** `Requirements/RequirementSet.h / .cpp`

---

# Why a Set Layer?

Without a set layer, every consuming system that needs grouping logic either hardcodes it or manages raw requirement arrays manually. The set layer standardises this:

- **`URequirementList`** — AND-all, the common case. Cheaper to author and evaluate. Supports OR/NOT logic inline via `URequirement_Composite` children.

Consuming systems and the Watcher System always hold a `TObjectPtr<URequirementSet>` — they never care which concrete type is underneath.

---

# `URequirementSet` — Abstract Base

```cpp
// Abstract base. Never instantiated directly.
// EditInlineNew: allows class picker when held as an Instanced UPROPERTY.
UCLASS(Abstract, EditInlineNew, CollapseCategories, BlueprintType)
class GAMECORE_API URequirementSet : public UObject
{
    GENERATED_BODY()
public:
    // Synchronous evaluation of the full set.
    UFUNCTION(BlueprintCallable, Category = "Requirements")
    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const PURE_VIRTUAL(URequirementSet::Evaluate, return FRequirementResult::Fail(););

    // Returns true if any requirement in the set requires async evaluation.
    virtual bool IsAsync() const PURE_VIRTUAL(URequirementSet::IsAsync, return false;);

    // Async evaluation. Fires OnComplete on the game thread once all requirements resolve.
    virtual void EvaluateAsync(const FRequirementContext& Context,
                               TFunction<void(FRequirementResult)> OnComplete) const;

    // Populates OutEvents with the union of all requirements' watched events.
    // Called by URequirementWatcherComponent during set registration.
    virtual void CollectWatchedEvents(FGameplayTagContainer& OutEvents) const PURE_VIRTUAL(URequirementSet::CollectWatchedEvents,);

    // Returns all requirements in the set (flat, for cache array sizing).
    virtual TArray<URequirement*> GetAllRequirements() const PURE_VIRTUAL(URequirementSet::GetAllRequirements, return {};);
};
```

---

# `URequirementList` — AND-All

The default and simplest set type. Every requirement must pass. Evaluation short-circuits on first failure.

```cpp
UCLASS(DisplayName = "Requirement List (AND-all)")
class GAMECORE_API URequirementList : public URequirementSet
{
    GENERATED_BODY()
public:
    // All requirements in this list must pass.
    // Array order is evaluation order — cheap sync checks first, async last.
    UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = "Requirements")
    TArray<TObjectPtr<URequirement>> Requirements;

    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const override;
    virtual bool IsAsync() const override;
    virtual void EvaluateAsync(const FRequirementContext& Context,
                               TFunction<void(FRequirementResult)> OnComplete) const override;
    virtual void CollectWatchedEvents(FGameplayTagContainer& OutEvents) const override;
    virtual TArray<URequirement*> GetAllRequirements() const override;
};
```

`Evaluate` delegates to `URequirementLibrary::EvaluateAll`. `EvaluateAsync` delegates to `URequirementLibrary::EvaluateAllAsync`. No new evaluation logic is introduced here.

---

---

# `FRequirementSetRuntime` — Per-Player Cache

The Watcher System maintains one `FRequirementSetRuntime` per registered set per player. This struct holds the per-player evaluation cache — it is never part of the shared `URequirementSet` asset.

```cpp
// Uniquely identifies a registered requirement set within a player's watcher component.
// Issued by URequirementWatcherComponent::RegisterSet.
struct FRequirementSetHandle
{
    uint32 Id = 0;
    bool IsValid() const { return Id != 0; }
};

// Per-player runtime state for one registered URequirementSet.
// Lives in URequirementWatcherComponent. Never replicated.
USTRUCT()
struct FRequirementSetRuntime
{
    GENERATED_BODY()

    // The shared set asset. Not per-player — many players may reference the same asset.
    UPROPERTY()
    TObjectPtr<URequirementSet> Asset;

    // Parallel to Asset->GetAllRequirements().
    // Indexed by requirement position in the flat list.
    // CachedTrue entries for monotonic requirements are never re-evaluated.
    TArray<ERequirementCacheState> CachedResults;

    // Authority mode for this set. Determined by the owning system at registration.
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

# `ERequirementEvalAuthority` — Per-Set Network Authority

Authority is a property of the **set registration**, not of individual requirements. The same `URequirement_HasItem` can be `ServerOnly` in a world event and `ClientValidated` in a quest unlock — the requirement asset is unchanged; only the set's authority differs.

```cpp
UENUM(BlueprintType)
enum class ERequirementEvalAuthority : uint8
{
    // Server evaluates only. Client never receives result until server decides.
    // Use for all gameplay-gating checks (loot, quest progression, ability use).
    ServerOnly UMETA(DisplayName = "Server Only"),

    // Client evaluates only. Server never checks.
    // Use for purely cosmetic or UI-gating checks (show tooltip, grey-out button).
    ClientOnly UMETA(DisplayName = "Client Only"),

    // Client evaluates for responsiveness. On all-pass, client fires a Server RPC.
    // Server re-evaluates fully from its own context — never trusts the client result.
    // Use for player-facing unlocks where immediate UI feedback matters (quest available).
    ClientValidated UMETA(DisplayName = "Client Validated"),
};
```

**Security note on `ClientValidated`.** The server RPC must trigger a full server-side re-evaluation using `URequirementLibrary::EvaluateAll` with a server-constructed `FRequirementContext`. The client RPC is only a hint — it cannot bypass the server check.

---

# Consuming Systems — Integration Pattern

```cpp
// In a Data Asset (e.g. UQuestDefinition):
UPROPERTY(EditDefaultsOnly, Instanced, Category = "Requirements")
TObjectPtr<URequirementSet> UnlockRequirements;

// One-shot server-side check (e.g. confirming an action RPC):
FRequirementContext Ctx;
Ctx.PlayerState = PS;
Ctx.World = GetWorld();
FRequirementResult Result = UnlockRequirements->Evaluate(Ctx);

// Watched registration (e.g. quest system tracking available quests):
URequirementWatcherComponent* Watcher = PS->GetComponentByClass<URequirementWatcherComponent>();
FRequirementSetHandle Handle = Watcher->RegisterSet(
    QuestDef->UnlockRequirements,
    ERequirementEvalAuthority::ClientValidated,
    FOnRequirementSetDirty::CreateUObject(this, &UQuestComponent::OnQuestRequirementsDirty)
);
```

Store the `FRequirementSetHandle`. Call `Watcher->UnregisterSet(Handle)` when the quest is accepted, completed, or abandoned to prevent leaking subscriptions.

---

# Known Limitations

- **`CollectWatchedEvents` on deep expression trees has upfront cost.** Called once at `RegisterSet` time — not per-frame. Acceptable for sets with up to ~50 nodes; pathological trees with hundreds of nested composites should be restructured.
- **Unregistering sets is the caller's responsibility.** The watcher component does not track set lifetime. Leaking registrations wastes subscription slots and keeps stale cache entries alive.