# Core Data Structures

**Sub-page of:** [Interaction System — Enhanced Specification](../Interaction%20System%20317d261a36cf8196ae77fc3c2e1e352d.md)

This page defines all enums, structs, and data asset classes that make up the interaction system's data layer. Enums are defined first since they are referenced by every structure below. Structures are ordered by dependency — each type appears before the types that use it.

All types in this page live in the `GameCore` runtime module under `Source/GameCore/Interaction/`. Exact file locations are noted per section.

---

# Enums

**File:** `Interaction/Enums/InteractionEnums.h`

All interaction system enums live in a single dedicated header. This header is included by nearly every other file in the system — isolating it means changing an enum value does not trigger cascade recompiles of component headers.

```cpp
// Whether an entry requires a single press or a sustained hold to trigger.
// Drives the scanner's input handling and the hold state machine.
UENUM(BlueprintType)
enum class EInteractionInputType : uint8
{
    Press   UMETA(DisplayName = "Press"),
    Hold    UMETA(DisplayName = "Hold")
};

// The visible state of an interaction entry as seen by the client.
// Available / Occupied / Cooldown / Disabled are set server-side and replicated
// via FFastArraySerializer. They reflect authoritative gameplay state.
// Locked is the only client-evaluated state — it is never replicated and never
// set by the server. The client assigns it when a tag pre-filter or
// EntryRequirements evaluation returns false.
UENUM(BlueprintType)
enum class EInteractableState : uint8
{
    Available   UMETA(DisplayName = "Available"),
    Occupied    UMETA(DisplayName = "Occupied"),    // Another actor is using this entry
    Cooldown    UMETA(DisplayName = "Cooldown"),    // Entry is temporarily unavailable
    Locked      UMETA(DisplayName = "Locked"),      // CLIENT ONLY — tag or requirement gate failed
    Disabled    UMETA(DisplayName = "Disabled")     // Administratively off — no prompt shown
};

// The reason a server-side interaction request was rejected.
// Sent to the instigating client via the pawn's ClientRPC_OnInteractionRejected for UI feedback.
// Values map to the eight-step server validation chain in UInteractionComponent.
UENUM(BlueprintType)
enum class EInteractionRejectionReason : uint8
{
    OutOfRange       UMETA(DisplayName = "Out of Range"),
    EntryNotFound    UMETA(DisplayName = "Entry Not Found"),
    EntryUnavailable UMETA(DisplayName = "Entry Unavailable"),  // Disabled, Occupied, or Cooldown
    TagMismatch      UMETA(DisplayName = "Tag Requirement Not Met"),
    ConditionFailed  UMETA(DisplayName = "Condition Not Met")   // EntryRequirements rejected
};

// Internal hold state machine. Used privately by UInteractionScannerComponent.
// Not exposed to Blueprint or UI — use OnHoldProgressChanged and OnHoldCancelled delegates instead.
UENUM()
enum class EInteractionHoldState : uint8
{
    Idle,
    Holding,
    Completed,
    Cancelled
};

// Controls how UInteractionComponent::ResolveOptions() shapes its output.
// Configured as a UPROPERTY on UInteractionScannerComponent — one setting per scanner.
UENUM(BlueprintType)
enum class EResolveMode : uint8
{
    // One winner per InteractionGroupTag — the highest-priority non-Locked candidate in each group.
    // Locked entries never win a group slot. Use for standard HUD interaction prompts.
    Best    UMETA(DisplayName = "Best"),

    // All candidates sorted by (GroupTag asc, OptionPriority desc).
    // Locked entries are included with their ConditionLabel for greyed-out display.
    // Use for inspect / examine UIs that show everything an actor offers.
    All     UMETA(DisplayName = "All")
};
```

---

# `FInteractionEntryConfig` — Static Config Struct

**File:** `Interaction/Data/InteractionEntryConfig.h`

All immutable, designer-authored data for a single interaction entry. Never replicated — lives identically on the server and all clients as part of a `UInteractionEntryDataAsset` or an inline array on `UInteractionComponent`. Must never be modified at runtime.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FInteractionEntryConfig
{
    GENERATED_BODY()

    // ── Availability: Tag Gates ───────────────────────────────────────────────

    // Tags the SOURCE actor (player pawn) must ALL have for this entry to be considered.
    // Evaluated client-side during resolution and server-side during validation.
    // Near-zero cost (bitset AND). Covers the majority of cases — prefer over TagQuery.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Availability")
    FGameplayTagContainer SourceRequiredTags;

    // Tags the TARGET actor (this interactable) must ALL have.
    // Same evaluation rules as SourceRequiredTags.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Availability")
    FGameplayTagContainer TargetRequiredTags;

    // Advanced AND/OR/NOT tag query on the source actor. Evaluated only if non-empty.
    // More expensive than tag containers — use for complex multi-condition gates only.
    // Applied after SourceRequiredTags if both are set (both must pass).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Availability|Advanced")
    FGameplayTagQuery SourceTagQuery;

    // Advanced AND/OR/NOT tag query on the target actor.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Availability|Advanced")
    FGameplayTagQuery TargetTagQuery;

    // ── Availability: Requirements ────────────────────────────────────────────

    // Optional requirement array for this entry.
    // Evaluated client-side (for Locked display) and server-side (authoritative gate).
    // Replaces IInteractionConditionProvider — no actor implementation required.
    //
    // The array is always AND — every element must pass. For OR or NOT logic on a
    // specific condition, add a URequirement_Composite as one element of the array
    // with the appropriate operator. The composite handles its children internally.
    //
    // CONSTRAINTS:
    //   - All requirements in the array (including composite children) must be
    //     synchronous (IsAsync() == false).
    //     Detected at BeginPlay via URequirementLibrary::ValidateRequirements
    //     with bRequireSync = true. Logged as an error in development builds.
    //   - Client-side evaluation must only read locally cached, replicated state.
    //     Requirements that cannot satisfy this must return Pass on the client.
    //   - The server evaluates authoritatively — FRequirementContext is constructed
    //     from the RPC connection, never from client-provided data.
    //
    // FRequirementContext fields available during evaluation:
    //   Context.Instigator  = the interacting pawn (source)
    //   Context.PlayerState = the pawn's PlayerState
    //   Context.World       = GetWorld()
    //
    // When empty, this validation step is skipped entirely — no overhead.
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Availability")
    TArray<TObjectPtr<URequirement>> EntryRequirements;

    // ── Grouping & Priority ───────────────────────────────────────────────────

    // The interaction group this entry competes within.
    // In EResolveMode::Best, only the highest-priority entry per group is shown.
    // Use this to control which input slot an entry occupies in the UI.
    // Example groups: Interaction.Group.Primary, Interaction.Group.Secondary
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Priority")
    FGameplayTag InteractionGroupTag;

    // Tiebreaker within the group. Higher value wins.
    // Example: QuestTurnIn (300) beats Shop (200) beats Examine (100).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Priority")
    int32 OptionPriority = 100;

    // If true and this is the highest-priority Available exclusive candidate,
    // it becomes the ONLY resolved option — all other entries are suppressed.
    // Use for interactions that demand undivided player attention (cutscene trigger,
    // critical quest event, boarding a ship).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Priority")
    bool bExclusive = false;

    // ── Input & UI ────────────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
    EInteractionInputType InputType = EInteractionInputType::Press;

    // Required hold duration in seconds. Ignored when InputType == Press.
    // Minimum 0.1s enforced by meta ClampMin.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction",
        meta = (EditCondition = "InputType == EInteractionInputType::Hold", ClampMin = "0.1"))
    float HoldTimeSeconds = 1.5f;

    // Localizable action verb shown in the interaction prompt.
    // Examples: "Talk", "Loot", "Turn In", "Board", "Harvest".
    // Accessed by pointer in FResolvedInteractionOption — never copied.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
    FText Label;

    // Enhanced Input action asset. The UI resolves the correct key icon and binding label
    // for the current input device and respects player remapping automatically.
    // Soft reference — loads only when a prompt widget needs to display.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
    TSoftObjectPtr<UInputAction> InputAction;

    // Optional per-entry icon that bypasses the component's IconDataAsset state mapping.
    // Use for entries with unique iconography that doesn't fit the standard state icons
    // (faction crest, quest marker, warning skull, special ability icon).
    // Also used as the Locked-state icon when EntryRequirements fails — there is no
    // per-condition icon override. If null, icon resolution falls through to the
    // component's IconDataAsset.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
    TSoftObjectPtr<UTexture2D> EntryIconOverride;
};
```

> **`EntryRequirements` is `Instanced`.** The Details panel shows each array element with a class picker — designers pick a `URequirement` subclass per slot. For OR/NOT logic on a specific slot, pick `URequirement_Composite` and configure its children inline. Each entry owns its own instanced requirements independently.
> 

> **No per-condition icon override.** Use `EntryIconOverride` on the config for a static locked-state icon, or handle dynamic iconography in the UI widget based on `ConditionLabel` content.
> 

> **Target actor in requirements.** `FRequirementContext` carries the instigating pawn as `Instigator`, not the target interactable. Tag-based conditions on the target actor should use `TargetRequiredTags` / `TargetTagQuery` (the fast path). If a requirement genuinely needs the target actor, it must retrieve it via the pawn's targeting component or a world subsystem — never via a hard dependency on the interactable's type.
> 

---

# `UInteractionEntryDataAsset` — Reusable Entry Definition

**File:** `Interaction/Data/InteractionEntryDataAsset.h / .cpp`

A `UDataAsset` subclass wrapping a single `FInteractionEntryConfig`. Allows designers to create named, reusable interaction definitions in the Content Browser. Multiple `UInteractionComponent` instances across the project reference the same asset — one asset, zero per-component data duplication, no per-component UObject allocation, no additional GC pressure beyond the asset itself.

```cpp
UCLASS(Blueprintable, BlueprintType)
class GAMECORE_API UInteractionEntryDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    // ShowOnlyInnerProperties flattens the Config struct in the Details panel —
    // designers see all fields directly without expanding a nested struct.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entry", meta = (ShowOnlyInnerProperties))
    FInteractionEntryConfig Config;
};
```

> **Why `UDataAsset` over `EditInlineNew` UObject?** `EditInlineNew` creates one UObject instance per component per entry — a Shop NPC and a Quest NPC each get their own separate object with identical data. A DataAsset is one asset referenced by N components. For an MMORPG with hundreds of interactable actors sharing common entry types (Shop, Talk, Quest, Harvest), DataAssets eliminate that duplication entirely. The GC only needs to track one object, not one per actor.
> 

> **No `CanInteract` or `OnInteractionConfirmed` hooks on the DataAsset.** These responsibilities belong to `UInteractionComponent`, not to the entry definition. Confirmation is handled by the component's `OnInteractionConfirmed` delegate, which game systems bind to. Keeping the DataAsset as pure data eliminates the temptation to embed game logic in what should be a configuration object, and removes any coupling between the asset class and game-specific systems.
> 

---

# Entry Storage and the Unified Flat Index

**Defined on:** `UInteractionComponent` — see [UInteractionComponent](UInteractionComponent%20317d261a36cf8005b0f5c31343f4a4ef.md) for the full class definition.

`UInteractionComponent` stores entries in two arrays that are indexed together as a single flat sequence. This unified index is the stable identifier used in all RPCs, all net state items, and all resolved options. It must never change after `BeginPlay`.

```cpp
// DataAsset entries — reusable, Content Browser-managed.
// Hard references: assets must be loaded to read config.
// Acceptable since the component is always loaded when its actor is in the world.
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entries")
TArray<TObjectPtr<UInteractionEntryDataAsset>> Entries;

// Inline struct entries — for one-off interactions that don't warrant a named asset.
// Zero UObject overhead. Zero GC pressure beyond the component itself.
// Prefer DataAssets for anything shared or reused across actors.
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entries")
TArray<FInteractionEntryConfig> InlineEntries;
```

```jsx
Unified flat index mapping:

Index [0 .. Entries.Num() - 1]
    → DataAsset entries, in array order

Index [Entries.Num() .. Entries.Num() + InlineEntries.Num() - 1]
    → Inline entries, in array order
```

`GetConfigAtIndex(uint8 Index) → const FInteractionEntryConfig*` is the single read access point used by all internal logic — the resolver, the validator, and the net state callbacks. It handles the two-array split transparently and returns `nullptr` for out-of-range indices.

> **`uint8` index caps at 255 entries per component.** Enforced by editor validation at design time. This is intentional — a single actor with more than 255 interaction entries is a design problem, not an engine limitation to work around. The `uint8` saves one byte on every RPC and every net state item.
> 

> **Entry arrays must not be resized after `BeginPlay`.** The flat index is baked into replicated net state and in-flight RPCs. Adding or removing entries at runtime would desync the index mapping between server and clients. Use `SetEntryServerEnabled(false)` or `SetEntryState(Disabled)` to hide entries dynamically without touching the array.
> 

---

# `FInteractionEntryNetState` — Replicated Runtime State

**File:** `Interaction/Data/InteractionNetState.h`

The only data replicated per entry. Static config is never sent over the wire. One `FInteractionEntryNetState` item exists per entry, delta-serialized as part of `FInteractionEntryNetStateArray` via `FFastArraySerializer` — only changed items are serialized and sent per replication cycle.

```cpp
USTRUCT()
struct FInteractionEntryNetState : public FFastArraySerializerItem
{
    GENERATED_BODY()

    // Stable flat index. Set at BeginPlay, never changes.
    // Used by PostReplicatedChange to identify which entry changed.
    UPROPERTY()
    uint8 EntryIndex = 0;

    // Gameplay state managed by game systems via SetEntryState().
    // Available / Occupied / Cooldown / Disabled are valid server-set values.
    // Locked is never stored here — it is a client-only evaluation result.
    UPROPERTY()
    EInteractableState State = EInteractableState::Available;

    // Administrative enable flag. Managed independently from State.
    // When false, the entry behaves as Disabled on all clients regardless of State.
    // Use for live-ops control, bug mitigation, or server-driven feature flags.
    // Kept separate from State so game systems can manage gameplay state freely
    // without accidentally re-enabling an administratively disabled entry.
    UPROPERTY()
    bool bServerEnabled = true;

    // ── FFastArraySerializer Callbacks ────────────────────────────────────────
    // These fire on clients after replication delivers an update.
    // All three delegate to the OwningComponent to broadcast OnEntryStateChanged.

    void PostReplicatedAdd(const struct FInteractionEntryNetStateArray& Array);
    void PostReplicatedChange(const struct FInteractionEntryNetStateArray& Array);
    void PreReplicatedRemove(const struct FInteractionEntryNetStateArray& Array);
};

USTRUCT()
struct FInteractionEntryNetStateArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FInteractionEntryNetState> Items;

    UPROPERTY(NotReplicated)
    TObjectPtr<UInteractionComponent> OwningComponent;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<
            FInteractionEntryNetState,
            FInteractionEntryNetStateArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FInteractionEntryNetStateArray>
    : TStructOpsTypeTraitsBase2<FInteractionEntryNetStateArray>
{
    enum { WithNetDeltaSerializer = true };
};
```

> **`bServerEnabled` is separate from `State` by design.** `State` reflects gameplay conditions (Occupied, Cooldown, Available) and is managed by game systems. `bServerEnabled` is an administrative override for live-ops or server operators. Keeping them separate means a game system can freely set `State = Occupied` without accidentally re-enabling an entry that was administratively disabled, and an operator can disable a bugged entry without disturbing the gameplay state machine.
> 

> **`OwningComponent` is the bridge between replication and the delegate system.** `FFastArraySerializer` callbacks fire on structs, not UObjects — they have no direct access to delegates. `OwningComponent` provides that access. On the server it is set in `BeginPlay`. On clients it is set when `PostReplicatedAdd` fires for the first time, which is guaranteed to occur before any `PostReplicatedChange` calls.
> 

---

# `FResolvedInteractionOption` — Client-Side Resolution Output

**File:** `Interaction/ResolvedInteractionOption.h`

Produced by `UInteractionComponent::ResolveOptions()`. Consumed exclusively by the UI layer. Never replicated — this struct exists only on the owning client and is rebuilt on every re-resolve. It must not be cached across frames.

All data from `FInteractionEntryConfig` is accessed by pointer (not copied) to avoid `FText` reference-count churn and soft-pointer copy overhead on the hot resolution path.

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FResolvedInteractionOption
{
    GENERATED_BODY()

    // The UInteractionComponent that produced this option.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UInteractionComponent> SourceComponent;

    // Flat entry index within SourceComponent. Passed verbatim to ServerRequestInteract.
    UPROPERTY(BlueprintReadOnly)
    uint8 EntryIndex = 0;

    // ── Config Data (pointers / soft refs — no copies) ────────────────────────

    // Pointer into the config's Label FText.
    // NOT a UPROPERTY — raw pointers cannot be exposed to Blueprint's reflection system.
    // Blueprint widgets should call GetLabel() (a BlueprintCallable wrapper) or use
    // ConditionLabel when State == Locked and ConditionLabel is non-empty.
    const FText* Label = nullptr;

    UPROPERTY(BlueprintReadOnly)
    TSoftObjectPtr<UInputAction> InputAction;

    // Per-entry icon override from FInteractionEntryConfig. May be null.
    // Used for both the normal state and the Locked state — there is no separate
    // condition icon override. See Icon Resolution Order below.
    UPROPERTY(BlueprintReadOnly)
    TSoftObjectPtr<UTexture2D> EntryIconOverride;

    UPROPERTY(BlueprintReadOnly)
    EInteractionInputType InputType = EInteractionInputType::Press;

    UPROPERTY(BlueprintReadOnly)
    float HoldTimeSeconds = 0.0f;

    UPROPERTY(BlueprintReadOnly)
    FGameplayTag GroupTag;

    // ── Evaluated Runtime State ───────────────────────────────────────────────

    UPROPERTY(BlueprintReadOnly)
    EInteractableState State = EInteractableState::Available;

    // ── Requirement Failure Output (only populated when State == Locked) ───────

    // The FailureReason from FRequirementResult when EntryRequirements evaluation failed.
    // Populated by ResolveOptions when the composite returns bPassed == false.
    // When non-empty and State == Locked, the UI should show this instead of Label.
    // Examples: "Requires Golden Key", "Shop closes at dawn", "Level 20 required".
    // Empty FText when State != Locked or no requirement provided a reason.
    UPROPERTY(BlueprintReadOnly)
    FText ConditionLabel;
};
```

## Icon Resolution Order

The UI widget resolves the display icon for an option using this priority chain. Evaluate top-to-bottom and use the first non-null result.

```jsx
1. EntryIconOverride is set
       → use EntryIconOverride
         (entry-specific art — applies in all states including Locked)

2. SourceComponent->GetIconDataAsset() is non-null
       → use IconDataAsset->GetIconForState(State)
         (component-level state mapping: Available/Occupied/Cooldown/Locked icons)

3. None of the above
       → null — widget hides the icon slot gracefully
```

> **`ConditionIconOverride` is removed.** The former `IInteractionConditionProvider::CanSeeInteraction` could return a per-condition icon. This field is not available on `FRequirementResult`. Use `EntryIconOverride` on the config for a static locked-state icon. Dynamic per-condition icons should be handled in the UI widget based on `ConditionLabel` content.
> 

> **`Label` is a raw `const FText*` and cannot be a `UPROPERTY`.** UE's reflection system does not support raw pointers as Blueprint-visible properties. The pointer is valid for the lifetime of the resolved option. Blueprint widgets should use `ConditionLabel` when `State == Locked` and `ConditionLabel` is non-empty, otherwise access `Label` through a `BlueprintCallable` C++ helper that dereferences safely.
> 

> **Do not cache `FResolvedInteractionOption` across frames.** The `SourceComponent` pointer and `Label` pointer are valid only for the lifetime of the scanner's `ResolvedBuffer`. The buffer is reset on every re-resolve. UI widgets should copy display values (label text, state, hold time) into their own widget state on receipt of `OnResolvedOptionsChanged`, not hold a reference to the struct itself.
>