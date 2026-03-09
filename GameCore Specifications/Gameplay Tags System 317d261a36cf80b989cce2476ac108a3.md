# Gameplay Tags System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

This document defines the Gameplay Tags System: a thin abstraction layer that gives any GameCore system uniform, implementation-agnostic access to an actor's gameplay tags — regardless of whether that actor uses GAS, a lightweight component, or a custom tag store.

---

# 1. Purpose and Design Goals

Unreal's `FGameplayTag` and `FGameplayTagContainer` are excellent data primitives, but accessing them on an arbitrary actor requires knowing what system backs them — a `UAbilitySystemComponent` for GAS actors, a custom component for non-GAS actors, or something else entirely. Without an abstraction layer, every system that reads tags must branch on actor type, import GAS headers, or use fragile cast chains.

`ITaggedInterface` solves this with a single contract: any actor that implements it can be queried for tags in one consistent way. The backing store — GAS, `UGameplayTagComponent`, or anything else — is invisible to the caller.

**Design goals:**

- **Zero coupling to GAS.** Systems that read tags never import `AbilitySystemComponent.h`. They call `ITaggedInterface` methods only.
- **No per-call allocation.** `GetGameplayTags()` returns a const reference — no copies, no temporaries on the hot path.
- **Authority-safe mutation.** Tag add/remove is authority-only by convention. The interface documents this; callers enforce it.
- **Replication-transparent.** Callers do not need to know whether tags are replicated via GAS, Push Model, or not at all. That is the implementing actor's concern.
- **Blueprint-accessible.** All query and mutation methods are callable from Blueprint despite the C++ hot-path optimisation on `GetGameplayTags()`.

---

# 2. `ITaggedInterface`

The interface contract. Implemented by any actor that participates in tag-based filtering, condition checking, or state management across GameCore systems.

```cpp
UINTERFACE(MinimalAPI, BlueprintType, NotBlueprintable)
class GAMECORE_API UTaggedInterface : public UInterface { GENERATED_BODY() };

class GAMECORE_API ITaggedInterface
{
    GENERATED_BODY()

public:
    // ── Primary Query (C++ hot path) ──────────────────────────────────────────

    // Returns the actor's full tag container by const reference.
    // Pure virtual C++ — intentionally not a UFUNCTION.
    // Called by GameCore systems on the resolution hot path (every scan period,
    // every server validation). No virtual dispatch overhead beyond the one call,
    // no Blueprint thunk, no copy.
    // Implementations must return a stable reference valid for the actor's lifetime.
    virtual const FGameplayTagContainer& GetGameplayTags() const = 0;

    // ── Convenience Queries (Blueprint + C++) ─────────────────────────────────

    // Returns true if the actor has the given tag.
    // Default implementation delegates to GetGameplayTags() — override only if
    // the backing store offers a faster single-tag lookup.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tags")
    bool HasGameplayTag(FGameplayTag Tag) const;
    virtual bool HasGameplayTag_Implementation(FGameplayTag Tag) const
    { return GetGameplayTags().HasTag(Tag); }

    // Returns true if the actor has ALL tags in the container.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tags")
    bool HasAllGameplayTags(const FGameplayTagContainer& Tags) const;
    virtual bool HasAllGameplayTags_Implementation(const FGameplayTagContainer& Tags) const
    { return GetGameplayTags().HasAll(Tags); }

    // Returns true if the actor has ANY tag in the container.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tags")
    bool HasAnyGameplayTags(const FGameplayTagContainer& Tags) const;
    virtual bool HasAnyGameplayTags_Implementation(const FGameplayTagContainer& Tags) const
    { return GetGameplayTags().HasAny(Tags); }

    // ── Mutation (Authority-only by convention) ───────────────────────────────

    // Add a tag to this actor's container.
    // AUTHORITY ONLY. Callers must check HasAuthority() before calling.
    // Implementations are responsible for replication — the interface makes no
    // guarantee about when or whether the change reaches clients.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tags")
    void AddGameplayTag(FGameplayTag Tag);
    virtual void AddGameplayTag_Implementation(FGameplayTag Tag) {}

    // Remove a tag from this actor's container.
    // AUTHORITY ONLY. Same replication contract as AddGameplayTag.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tags")
    void RemoveGameplayTag(FGameplayTag Tag);
    virtual void RemoveGameplayTag_Implementation(FGameplayTag Tag) {}
};
```

> **Why is `GetGameplayTags()` pure virtual C++ and not a `UFUNCTION`?** It is called by GameCore systems on the resolution hot path — the interaction scanner calls it every `ScanPeriod` for the disabling tag check, and the resolver calls it per entry per candidate. A `BlueprintNativeEvent` adds a thunk, a vtable indirection through the Blueprint VM, and prevents inlining. A pure virtual C++ function gets one vtable call, inlineable at the call site, with zero Blueprint overhead. Convenience methods (`HasGameplayTag`, `HasAllGameplayTags`, `HasAnyGameplayTags`) are `BlueprintNativeEvent` because they are called from Blueprint and are not on the hot path.
> 

> **`NotBlueprintable` on `UTaggedInterface`.** The interface cannot be implemented in Blueprint directly because `GetGameplayTags()` is a pure virtual C++ function with no Blueprint equivalent. Blueprint actors that need tags attach `UGameplayTagComponent` instead, which implements the interface in C++ and exposes all mutation methods to Blueprint.
> 

---

# 3. `UGameplayTagComponent`

The canonical `ITaggedInterface` implementation for actors that do not use GAS. A lightweight replicated component that stores a `FGameplayTagContainer` and notifies listeners when tags change — both at the per-tag level and at the container level, mirroring GAS's `RegisterGameplayTagEvent` pattern.

```cpp
// Fires when a specific tag is added to or removed from the container.
// Mirrors GAS's RegisterGameplayTagEvent pattern for consistent usage across both systems.
//
// AUTHORITY + CLIENT (add/remove): Fires on the SERVER immediately when AddGameplayTag
// or RemoveGameplayTag is called. Also fires on the LOCAL OWNER if it calls mutation
// directly (non-replicated path). Does NOT fire on non-owning clients — they receive
// only OnTagsChanged via OnRep_Tags. See Section 6 (Known Limitations).
//
// NewCount: 1 when the tag was added, 0 when removed.
// UpdatedContainer: the full container state after the change.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnGameplayTagChanged,
    FGameplayTag,                   Tag,
    int32,                          NewCount,
    const FGameplayTagContainer&,   UpdatedContainer);

// Fires whenever the container changes for any reason (add, remove, or bulk SetGameplayTags).
//
// SERVER: fires immediately on every mutation.
// CLIENTS: fires via OnRep_Tags when the replicated container arrives.
//          All clients receive this — use it as the reliable cross-machine notification.
// Delivers the full updated container. Diff against a cached snapshot if per-tag
// resolution is needed on the client side.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameplayTagContainerChanged,
    const FGameplayTagContainer&, UpdatedContainer);

UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UGameplayTagComponent : public UActorComponent, public ITaggedInterface
{
    GENERATED_BODY()

    // The authoritative tag container.
    // Replicated via Push Model — only serialized when actually dirty.
    UPROPERTY(ReplicatedUsing = OnRep_Tags, EditAnywhere, BlueprintReadOnly, Category = "Tags")
    FGameplayTagContainer Tags;

    UFUNCTION()
    void OnRep_Tags()
    {
        // CLIENTS ONLY — called by the replication system after a new container state arrives.
        // Per-tag OnTagChanged is NOT fired here — the rep callback only delivers the final
        // container, not a diff. Clients that need per-tag resolution must diff against a
        // locally cached snapshot inside their OnTagsChanged binding.
        OnTagsChanged.Broadcast(Tags);
    }

public:
    // ── ITaggedInterface ──────────────────────────────────────────────────────

    virtual const FGameplayTagContainer& GetGameplayTags() const override { return Tags; }

    virtual bool HasGameplayTag_Implementation(FGameplayTag Tag) const override
    { return Tags.HasTag(Tag); }

    virtual bool HasAllGameplayTags_Implementation(const FGameplayTagContainer& InTags) const override
    { return Tags.HasAll(InTags); }

    virtual bool HasAnyGameplayTags_Implementation(const FGameplayTagContainer& InTags) const override
    { return Tags.HasAny(InTags); }

    // Add a single tag. No-op if already present (avoids unnecessary dirty mark + replication).
    // Fires OnTagChanged (NewCount=1) and OnTagsChanged on the SERVER immediately.
    // Non-owning CLIENTS receive OnTagsChanged only via OnRep_Tags — NOT OnTagChanged.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Tags")
    virtual void AddGameplayTag_Implementation(FGameplayTag Tag) override
    {
        if (Tags.HasTagExact(Tag)) return;
        Tags.AddTag(Tag);
        MARK_PROPERTY_DIRTY_FROM_NAME(UGameplayTagComponent, Tags, this);
        OnTagChanged.Broadcast(Tag, 1, Tags);
        OnTagsChanged.Broadcast(Tags);
    }

    // Remove a single tag. No-op if not present (avoids unnecessary dirty mark + replication).
    // Fires OnTagChanged (NewCount=0) and OnTagsChanged on the SERVER immediately.
    // Non-owning CLIENTS receive OnTagsChanged only via OnRep_Tags — NOT OnTagChanged.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Tags")
    virtual void RemoveGameplayTag_Implementation(FGameplayTag Tag) override
    {
        if (!Tags.HasTagExact(Tag)) return;
        Tags.RemoveTag(Tag);
        MARK_PROPERTY_DIRTY_FROM_NAME(UGameplayTagComponent, Tags, this);
        OnTagChanged.Broadcast(Tag, 0, Tags);
        OnTagsChanged.Broadcast(Tags);
    }

    // ── Bulk Mutation ─────────────────────────────────────────────────────────

    // Replace the entire container in one operation.
    // More efficient than N Add/Remove calls — marks dirty once, one replication delta.
    // Diffs old vs. new to fire OnTagChanged correctly for each added/removed tag.
    // Fires OnTagChanged per changed tag (SERVER only) then OnTagsChanged once (SERVER + CLIENTS via rep).
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Tags")
    void SetGameplayTags(const FGameplayTagContainer& NewTags)
    {
        // Diff before overwrite so we can fire accurate per-tag events.
        FGameplayTagContainer Added;
        FGameplayTagContainer Removed;

        for (const FGameplayTag& OldTag : Tags)
            if (!NewTags.HasTagExact(OldTag)) Removed.AddTag(OldTag);

        for (const FGameplayTag& NewTag : NewTags)
            if (!Tags.HasTagExact(NewTag)) Added.AddTag(NewTag);

        Tags = NewTags;
        MARK_PROPERTY_DIRTY_FROM_NAME(UGameplayTagComponent, Tags, this);

        // SERVER only: per-tag events fired for each delta tag.
        for (const FGameplayTag& Tag : Removed) OnTagChanged.Broadcast(Tag, 0, Tags);
        for (const FGameplayTag& Tag : Added)   OnTagChanged.Broadcast(Tag, 1, Tags);

        // SERVER + CLIENTS (via rep): full container notification.
        OnTagsChanged.Broadcast(Tags);
    }

    // ── Delegates ─────────────────────────────────────────────────────────────

    // Per-tag delta event. Mirrors GAS's RegisterGameplayTagEvent pattern.
    // SERVER: fires on AddGameplayTag, RemoveGameplayTag, and SetGameplayTags.
    // CLIENTS: does NOT fire — clients receive OnTagsChanged via OnRep_Tags only.
    //          Client-side per-tag resolution: diff inside your OnTagsChanged binding.
    // NewCount: 1 = tag added, 0 = tag removed.
    UPROPERTY(BlueprintAssignable, Category = "Tags")
    FOnGameplayTagChanged OnTagChanged;

    // Full container changed event.
    // SERVER: fires on every mutation (add, remove, bulk set).
    // CLIENTS: fires via OnRep_Tags when replication delivers the updated container.
    //          This is the reliable cross-machine notification — bind here for client UI,
    //          client-side condition caches, and any logic that must run on all machines.
    UPROPERTY(BlueprintAssignable, Category = "Tags")
    FOnGameplayTagContainerChanged OnTagsChanged;
};
```

### Replication Setup (Push Model)

Push Model replication must be explicitly configured in `GetLifetimeReplicatedProps`:

```cpp
void UGameplayTagComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    FDoRepLifetimeParams Params;
    Params.bIsPushBased = true;
    Params.Condition = COND_None;  // Replicate to all relevant clients
    DOREPLIFETIME_WITH_PARAMS_FAST(UGameplayTagComponent, Tags, Params);
}
```

The owning module must enable Push Model in its `Build.cs`:

```csharp
// In GameCore.Build.cs
PrivateDependencyModuleNames.AddRange(new string[]
{
    "NetCore"  // Required for Push Model (MARK_PROPERTY_DIRTY_FROM_NAME)
});
```

> **Why Push Model?** Without Push Model, UE's replication system checks every replicated property on every actor every network tick to see if it changed — even if nothing changed. With Push Model, the property is only serialized and sent when `MARK_PROPERTY_DIRTY_FROM_NAME` is called. For a tag container that changes infrequently (player gains a quest tag, enters combat, etc.), this eliminates the comparison overhead entirely at steady state.
> 

> **No-op guards on Add/Remove.** `AddGameplayTag` checks `HasTagExact` before adding, and `RemoveGameplayTag` checks before removing. This prevents marking the container dirty — and triggering a replication update — when the operation is a no-op. Without this guard, calling `AddGameplayTag` with an already-present tag would still dirty the property and send an unnecessary delta to all clients.
> 

> **Two delegates, two audiences.** `OnTagChanged` is the per-tag delta event — useful for server-side systems that need to react to a specific tag appearing or disappearing (AI perception, condition providers, scanners). `OnTagsChanged` is the full-container event — the reliable cross-machine notification that fires on both server and all clients. When in doubt, bind to `OnTagsChanged`. Only bind to `OnTagChanged` when you specifically need per-tag granularity and are certain you are on the server.
> 

---

# 4. GAS Implementation Pattern

Actors using GAS implement `ITaggedInterface` by forwarding to their `UAbilitySystemComponent`. The interaction system and all other GameCore systems never import GAS headers — they only ever call `ITaggedInterface` methods.

```cpp
// In your GAS actor's header (game project, not GameCore):
class AMyCharacter : public ACharacter, public ITaggedInterface
{
    // ...
    virtual const FGameplayTagContainer& GetGameplayTags() const override;
    virtual void AddGameplayTag_Implementation(FGameplayTag Tag) override;
    virtual void RemoveGameplayTag_Implementation(FGameplayTag Tag) override;
};

// In the .cpp:
const FGameplayTagContainer& AMyCharacter::GetGameplayTags() const
{
    // AbilitySystemComponent->GetOwnedGameplayTags() returns a const ref
    // to the ASC's internal container — zero copy, stable reference.
    static FGameplayTagContainer EmptyContainer;
    if (!AbilitySystemComponent) return EmptyContainer;
    AbilitySystemComponent->GetOwnedGameplayTags(/* out */ CachedTagContainer);
    return CachedTagContainer;
}
```

> **Caching note for GAS.** `UAbilitySystemComponent::GetOwnedGameplayTags()` fills an out parameter rather than returning a reference. To satisfy the `const FGameplayTagContainer&` return contract of `GetGameplayTags()`, GAS actors must cache a `mutable FGameplayTagContainer CachedTagContainer` as a member and refresh it in `GetGameplayTags()`. This is a shallow copy of the ASC's internal container — cheap, but worth noting. An alternative is to return the ASC's `GetGameplayTagContainer()` directly if the ASC exposes it by reference (version-dependent).
> 

> **Tag change notifications for GAS.** GAS actors do not use `OnTagChanged` or `OnTagsChanged` — they use the ASC's native `RegisterGameplayTagEvent` callback system, which provides per-tag add/remove events with richer context (prediction keys, source objects). Both delegates are `UGameplayTagComponent`-specific. For GAS actors, bind directly to the ASC's tag event system from game-side code.
> 

---

# 5. Usage Guidelines

**When to use `ITaggedInterface` vs. direct GAS calls:**

Always use `ITaggedInterface` from GameCore systems and any code that must be actor-type agnostic. Use direct ASC calls only from systems that are explicitly GAS-aware and live in the game project.

**When to use `GetGameplayTags()` vs. `HasGameplayTag()`:**

Use `GetGameplayTags()` when you need to check multiple tags on the same actor in sequence — call once, hold the reference, call `HasAll` / `HasAny` / `Matches` directly on the container. Use `HasGameplayTag()` for single-tag Blueprint checks where convenience matters more than micro-performance.

**Mutation is always server-only:**

`AddGameplayTag` and `RemoveGameplayTag` must only be called from server-side code. The interface cannot enforce this at compile time — callers must check `HasAuthority()`. Violating this silently on a client will cause the tag to exist locally but not replicate, creating a state desync that is difficult to debug.

**Do not store raw pointers to `GetGameplayTags()` return values across frames:**

The reference is stable within a frame, but GAS actors may invalidate their internal container if the ASC is torn down or the actor is reinitialized. Cache the interface pointer, not the container pointer.

---

# 6. Known Limitations

**`OnTagChanged` does not fire on non-owning clients.** `OnTagChanged` is fired server-side on every tag mutation and provides per-tag delta events (`NewCount = 1` added, `0` removed). Non-owning clients only receive `OnTagsChanged` via `OnRep_Tags`, which delivers the final container state with no diff. Clients that need per-tag resolution must maintain a local snapshot of the previous container and diff inside their `OnTagsChanged` binding. This is the same limitation that exists in GAS's `RegisterGameplayTagEvent` in non-prediction contexts — it is a fundamental constraint of property-based replication, not a design flaw.

**`ITaggedInterface` has no tag-change notification at the interface level.** Both `OnTagChanged` and `OnTagsChanged` exist only on `UGameplayTagComponent`. Code holding a raw `ITaggedInterface*` that needs change notifications must cast to `UGameplayTagComponent` to bind, or poll `GetGameplayTags()`. Adding a virtual notification method to the interface would require all implementations — including GAS forwarding actors — to support it, which conflicts with GAS's own event model. Accepting this cast is the correct tradeoff.

**`GetGameplayTags()` is not thread-safe.** It returns a reference to an internal container that may be modified by server-side mutations on the game thread. Do not call from worker threads. All GameCore systems call it exclusively from the game thread.

**GAS `GetGameplayTags()` requires a container copy.** The forwarding implementation for GAS actors involves a shallow copy into a cached member, as described in Section 4. This is a known cost of satisfying the `const FGameplayTagContainer&` return contract when the ASC does not expose its internal container by reference directly.

---

# 7. File and Folder Structure

```
GameCore/
└── Source/
    └── GameCore/
        └── Tags/
            ├── TaggedInterface.h / .cpp          ← ITaggedInterface
            └── GameplayTagComponent.h / .cpp     ← UGameplayTagComponent
```

Both files belong in the runtime module. There is no editor-only code in this system — tag components have no custom details panel requirements beyond standard component editing.

The `Tags/` folder sits at the GameCore root level, not inside `Interaction/` or any other feature folder, reflecting that this system is shared infrastructure used by multiple GameCore features.

---

# 8. Corrections and Errata

## Section 4 — GAS `GetGameplayTags()` Implementation (Corrected)

The original Section 4 contained an incorrect implementation. The corrected `.cpp` is:

```cpp
const FGameplayTagContainer& AMyCharacter::GetGameplayTags() const
{
    // UAbilitySystemComponent::GetOwnedGameplayTags() returns a const FGameplayTagContainer&
    // directly in UE5 — no out parameter, no copy, no mutable cache member needed.
    static FGameplayTagContainer EmptyContainer;
    if (!AbilitySystemComponent) return EmptyContainer;
    return AbilitySystemComponent->GetOwnedGameplayTags();
}
```

> **Why the original was wrong.** The original showed `GetOwnedGameplayTags(/* out */ CachedTagContainer)` filling a `mutable` cache member and returning it by reference. In UE5, `UAbilitySystemComponent::GetOwnedGameplayTags()` takes **no parameters** and returns `const FGameplayTagContainer&` pointing directly into the ASC's internal `FGameplayTagCountContainer`. No cache, no copy, no `mutable` member required. The old out-parameter signature no longer exists. If you are integrating against a fork or modified engine, verify the signature in `AbilitySystemComponent.h` before adding any caching layer.
> 

> **Known Limitation update.** The entry in Section 6 ("GAS `GetGameplayTags()` requires a container copy") is **no longer accurate** as of the corrected implementation above. GAS actors now satisfy the `const FGameplayTagContainer&` return contract with zero copy. That Known Limitation entry should be disregarded.
> 

---

# 9. Requirement Integration — `URequirement_HasTag`

The Tags system provides one `URequirement` subclass, owned by the `Tags/` module. It follows the module boundary rules defined in the Requirement System specification: `Requirements/` has no dependency on `Tags/`; `Tags/` depends on `Requirements/`.

## `URequirement_HasTag`

Evaluates whether the **subject actor** (resolved from `FRequirementContext`) implements `ITaggedInterface` and owns a specific tag. Synchronous. No allocation on the hot path.

```cpp
// Tags/Requirements/RequirementHasTag.h

UCLASS(DisplayName = "Has Gameplay Tag")
class GAMECORE_API URequirement_HasTag : public URequirement
{
    GENERATED_BODY()

public:
    // The tag the subject must own for this requirement to pass.
    UPROPERTY(EditAnywhere, Category = "Requirement")
    FGameplayTag RequiredTag;

    // If true, uses HasTagExact — the subject must own this exact tag with no parent match.
    // If false (default), uses HasTag — parent tags satisfy the check (standard UE behaviour).
    UPROPERTY(EditAnywhere, Category = "Requirement")
    bool bExactMatch = false;

    virtual FRequirementResult Evaluate(const FRequirementContext& Context) const override
    {
        if (!RequiredTag.IsValid())
            return FRequirementResult::Fail(LOCTEXT("HasTag_InvalidTag", "Requirement has no tag configured."));

        // Resolve subject: prefer Instigator, fall back to PlayerState's pawn.
        AActor* Subject = Context.Instigator
            ? Context.Instigator
            : (Context.PlayerState ? Context.PlayerState->GetPawn() : nullptr);

        if (!Subject)
            return FRequirementResult::Fail(LOCTEXT("HasTag_NoSubject", "No valid subject actor for tag check."));

        ITaggedInterface* Tagged = Cast<ITaggedInterface>(Subject);
        if (!Tagged)
            return FRequirementResult::Fail(LOCTEXT("HasTag_NoInterface", "Subject does not implement ITaggedInterface."));

        const FGameplayTagContainer& Tags = Tagged->GetGameplayTags();
        const bool bPasses = bExactMatch ? Tags.HasTagExact(RequiredTag) : Tags.HasTag(RequiredTag);

        return bPasses
            ? FRequirementResult::Pass()
            : FRequirementResult::Fail(FText::Format(
                LOCTEXT("HasTag_Missing", "Missing required tag: {0}"),
                FText::FromName(RequiredTag.GetTagName())));
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(TEXT("Has Tag: %s%s"),
            *RequiredTag.ToString(),
            bExactMatch ? TEXT(" (exact)") : TEXT(""));
    }
#endif
};
```

## Subject Resolution

`URequirement_HasTag` resolves its subject in this priority order:

1. `Context.Instigator` — used when the requirement evaluates an interactable, NPC, or world actor.
2. `Context.PlayerState->GetPawn()` — fallback for player-facing checks (quest gates, ability prerequisites).

This covers the two dominant MMORPG use cases: **checking the player's own tags** (e.g. player must have `Status.Cursed` to enter a zone) and **checking a target actor's tags** (e.g. NPC must have `State.Downed` to be interactable).

## Usage Notes

**Single-tag only.** `URequirement_HasTag` checks one tag per instance. For multi-tag AND/OR logic, compose multiple instances under a `URequirement_Composite`. This is intentional — composites replace hardcoded boolean logic per the Requirement System design principles.

**Exact vs. hierarchical match.** Leave `bExactMatch = false` (default) in almost all cases — UE's hierarchical tag matching (`HasTag`) is the standard and expected behaviour. Only enable `bExactMatch` when a system must distinguish between `State.Downed` and `State.Downed.Unconscious` explicitly.

**Server authority.** Tag mutation is server-only. `URequirement_HasTag` reads `GetGameplayTags()` which is safe to call on both server and client — but authoritative gates must only call `EvaluateAll` server-side.

**Client display.** Safe to evaluate client-side for UI gating (greying out interaction prompts, disabling ability buttons). The result is non-authoritative — the server re-evaluates before any action is taken.

## File Location

```
GameCore/
└── Source/
    └── GameCore/
        └── Tags/
            ├── TaggedInterface.h / .cpp
            ├── GameplayTagComponent.h / .cpp
            └── Requirements/
                └── RequirementHasTag.h / .cpp     ← URequirement_HasTag
```

## Build.cs Dependency

`Tags/` must declare a dependency on `Requirements/` in `GameCore.Build.cs`. No additional modules required — `URequirement_HasTag` only uses `ITaggedInterface`, `FGameplayTag`, and `URequirement`, all resident in `GameCore`.

```csharp
// GameCore.Build.cs — already required; listed here for explicit traceability
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameplayTags",   // FGameplayTag, FGameplayTagContainer
    "GameCore",       // URequirement base (Requirements/ subfolder)
});
```