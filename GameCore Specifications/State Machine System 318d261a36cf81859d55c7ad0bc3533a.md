# State Machine System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The State Machine System is a data-driven, event-driven, graph-based finite state machine framework. It is game-agnostic and designed to be extended by any system — quests, ship states, NPC behaviour, UI flows — without modification to the core. States and transitions are authored as instanced UObjects inside a `UDataAsset` graph definition, edited via a custom graph editor, and driven at runtime by the owning system through explicit event calls. Execution authority is declared per-component and can be server-only, client-only, or replicated.

---

# System Units

| Unit | Class | Role |
| --- | --- | --- |
| Graph Definition | `UStateMachineAsset` | `UDataAsset` owning all state nodes and transitions. One asset per machine type. |
| State Node | `UStateNodeBase` | Instanced `UObject` representing one state. Subclassed per system. |
| Transition Rule | `UTransitionRule` | Instanced `UObject` evaluating a single condition. Subclassed per system. |
| Transition Definition | `FStateTransition` | USTRUCT linking FromState → ToState with a rule and composition mode. |
| Runtime Snapshot | `FStateMachineRuntime` | USTRUCT holding current state, history, and timestamps. Lives on the component. |
| Runtime Driver | `UStateMachineComponent` | Actor component that hosts an asset and drives execution. |
| Authority Mode | `EStateMachineAuthority` | Enum declaring server-only, client-only, or replicated execution per component. |

> `UStateNodeBase` and `UTransitionRule` are `UCLASS(Abstract, EditInlineNew)` — instanced UObjects owned by the asset, not standalone Content Browser assets. Same pattern as Behavior Tree Tasks and PCG Nodes. No per-asset overhead in the Content Browser.
> 

---

# Core Design Principles

- **The asset is the definition, the component is the runtime.** `UStateMachineAsset` carries no mutable state. All runtime data lives in `FStateMachineRuntime` on the component. The same asset can be shared across many components safely.
- **Event-driven by default, no tick.** The component does not tick. Transitions fire only when the owning system calls `RequestTransition` or `EvaluateTransitions`. The owning system decides when to poke the machine.
- **Authority is explicit.** `EStateMachineAuthority` is set at design time on the component. The component enforces it at runtime — it does not rely on the owning actor's net role. This makes components reusable across server-only, client-only, and replicated actors without logic changes.
- **Only current state replicates.** A single `FGameplayTag` travels over the wire. State node data, history, and rule state never replicate. Clients receive `OnStateChanged` and reconstruct any derived display state locally.
- **No cross-system imports at the core layer.** `UStateNodeBase` and `UTransitionRule` in `StateMachine/` have zero dependencies on game systems. Each extending system subclasses these types inside its own folder.
- **Subclassing is the extension mechanism.** There is no central registry. A quest system creates `UQuestStateNode : UStateNodeBase`. A ship system creates `UShipStateNode : UStateNodeBase`. The graph editor discovers all `EditInlineNew` subclasses automatically via UE reflection.

---

# How the Pieces Connect

**Authoring.** A designer or programmer creates a `UStateMachineAsset` (or a typed subclass like `UQuestStateMachineAsset`) in the Content Browser and opens it in the custom graph editor. States are added as nodes — each node is an instanced `UStateNodeBase` subclass chosen from a class picker showing all loaded subclasses. Transitions are drawn as edges, each referencing an instanced `UTransitionRule` (or left rule-free for unconditional transitions). An Entry node marks the initial state. Any-state transitions are configured in a sidebar panel.

**Runtime setup.** `UStateMachineComponent` is added to an Actor. `StateMachineAsset` is assigned and `AuthorityMode` is set. On `BeginPlay`, the component validates the asset, instantiates `FStateMachineRuntime`, enters the entry state (calling `OnEnter` on the first `UStateNodeBase`), and is ready. No tick is registered.

**Driving transitions.** The owning system calls either:

- `RequestTransition(TargetStateTag)` — when the owning system already knows the desired next state (direct push).
- `EvaluateTransitions(ContextObject)` — when the owning system signals that something relevant happened and the machine should check its rules.

On `EvaluateTransitions`, any-state rules are evaluated first, then rules from the current state's outgoing transitions in definition order. The first passing rule wins. `RequestTransition` bypasses rules entirely — it is a forced move and must only be called when the owning system has already validated the transition is correct.

**State change.** On transition: `OnExit` fires on the leaving node → `FStateMachineRuntime` updates current state and pushes to history → `OnEnter` fires on the entering node → `OnStateChanged` delegate broadcasts. On the server with `AuthorityMode != ClientOnly`, the replicated state tag is updated and pushed to clients via the normal replication path.

**Client reception.** Clients with `AuthorityMode == Both` receive the replicated `FGameplayTag`. `OnStateChanged` fires on the client, allowing cosmetic updates, UI sync, and animation state changes without any additional RPCs.

---

# `EStateMachineAuthority`

```cpp
UENUM(BlueprintType)
enum class EStateMachineAuthority : uint8
{
    // Machine runs on server only. Current state tag replicates to clients.
    // Clients receive OnStateChanged but cannot call RequestTransition or EvaluateTransitions.
    ServerOnly,

    // Machine runs on owning client only. Nothing replicates. Used for cosmetic-only machines.
    ClientOnly,

    // Server is authoritative. Current state tag replicates. Clients may run a local
    // predicted copy but server result always wins on conflict.
    Both
};
```

---

# `UStateMachineAsset`

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UStateMachineAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    // All state nodes in this machine. Instanced — each asset owns its own copies.
    UPROPERTY(EditDefaultsOnly, Instanced, Category = "States")
    TArray<TObjectPtr<UStateNodeBase>> States;

    // All transitions. Evaluated in order during EvaluateTransitions.
    UPROPERTY(EditDefaultsOnly, Category = "Transitions")
    TArray<FStateTransition> Transitions;

    // Transitions evaluated from any state regardless of current state (e.g. forced reset, death).
    // Checked before per-state transitions in EvaluateTransitions.
    UPROPERTY(EditDefaultsOnly, Category = "Transitions")
    TArray<FStateTransition> AnyStateTransitions;

    // Tag identifying the entry state. Must match a StateTag on one of the States entries.
    UPROPERTY(EditDefaultsOnly, Category = "States")
    FGameplayTag EntryStateTag;

    // Max history entries retained at runtime. 0 = history disabled.
    UPROPERTY(EditDefaultsOnly, Category = "Settings", meta = (ClampMin = 0, ClampMax = 32))
    int32 HistorySize = 8;

    // Editor: validate that all transition FromState/ToState tags resolve to known states.
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

    // Returns the node for a given state tag. Returns null if not found.
    UStateNodeBase* FindNode(const FGameplayTag& StateTag) const;
};
```

---

# `UStateNodeBase`

```cpp
UCLASS(Abstract, EditInlineNew, CollapseCategories, BlueprintType, Blueprintable)
class GAMECORE_API UStateNodeBase : public UObject
{
    GENERATED_BODY()

public:
    // Stable identifier for this state. Must be unique within the owning asset.
    UPROPERTY(EditDefaultsOnly, Category = "State")
    FGameplayTag StateTag;

    // Optional gameplay tags applied to the owning actor while this state is active.
    // Applied on OnEnter, removed on OnExit. Requires ITaggedInterface on the owner.
    UPROPERTY(EditDefaultsOnly, Category = "State")
    FGameplayTagContainer GrantedTags;

    // Called when the machine enters this state.
    // Component is the UStateMachineComponent driving this machine.
    virtual void OnEnter(UStateMachineComponent* Component) {}

    // Called when the machine exits this state.
    virtual void OnExit(UStateMachineComponent* Component) {}

    // Blueprint extension points.
    UFUNCTION(BlueprintImplementableEvent, Category = "State Machine")
    void BP_OnEnter(UStateMachineComponent* Component);

    UFUNCTION(BlueprintImplementableEvent, Category = "State Machine")
    void BP_OnExit(UStateMachineComponent* Component);

#if WITH_EDITOR
    // Short description shown on the node in the graph editor.
    virtual FString GetNodeDescription() const { return StateTag.ToString(); }

    // Color used for this node type in the graph editor. Override in subclasses.
    virtual FLinearColor GetNodeColor() const { return FLinearColor(0.2f, 0.4f, 0.8f); }
#endif
};
```

---

# `UTransitionRule`

```cpp
UCLASS(Abstract, EditInlineNew, CollapseCategories, BlueprintType, Blueprintable)
class GAMECORE_API UTransitionRule : public UObject
{
    GENERATED_BODY()

public:
    // Evaluate whether this rule passes.
    // Context is an optional object supplied by the owning system via EvaluateTransitions.
    // Must return immediately — no async, no deferred results.
    virtual bool Evaluate(UStateMachineComponent* Component, UObject* Context) const
    {
        return false;
    }

    UFUNCTION(BlueprintImplementableEvent, Category = "State Machine")
    bool BP_Evaluate(UStateMachineComponent* Component, UObject* Context) const;

#if WITH_EDITOR
    // Short label displayed on the transition edge in the graph editor.
    virtual FString GetRuleDescription() const { return TEXT("Rule"); }
#endif
};
```

---

# `FStateTransition`

```cpp
// Composition mode for multi-rule transitions.
UENUM(BlueprintType)
enum class ETransitionComposition : uint8
{
    // All rules must pass.
    AND,
    // Any one rule must pass.
    OR
};

USTRUCT(BlueprintType)
struct GAMECORE_API FStateTransition
{
    GENERATED_BODY()

    // Tag of the source state. Empty = any-state transition (used in AnyStateTransitions array).
    UPROPERTY(EditDefaultsOnly, Category = "Transition")
    FGameplayTag FromStateTag;

    // Tag of the destination state.
    UPROPERTY(EditDefaultsOnly, Category = "Transition")
    FGameplayTag ToStateTag;

    // Rules evaluated to determine if this transition can fire.
    // Empty = unconditional transition (always passes when evaluated).
    UPROPERTY(EditDefaultsOnly, Instanced, Category = "Transition")
    TArray<TObjectPtr<UTransitionRule>> Rules;

    // How multiple rules are composed.
    UPROPERTY(EditDefaultsOnly, Category = "Transition")
    ETransitionComposition Composition = ETransitionComposition::AND;

    // Priority. Higher values are evaluated first within the same FromState.
    UPROPERTY(EditDefaultsOnly, Category = "Transition")
    int32 Priority = 0;

    // Evaluate all rules against the current component and context.
    bool Evaluate(UStateMachineComponent* Component, UObject* Context) const;
};
```

---

# `FStateMachineRuntime`

```cpp
USTRUCT()
struct GAMECORE_API FStateMachineRuntime
{
    GENERATED_BODY()

    // Currently active state tag.
    UPROPERTY()
    FGameplayTag CurrentStateTag;

    // Previous state tags, newest first. Capped by UStateMachineAsset::HistorySize.
    UPROPERTY()
    TArray<FGameplayTag> History;

    // World time when the current state was entered.
    UPROPERTY()
    float CurrentStateEnterTime = 0.f;

    // Total number of transitions that have fired since BeginPlay.
    UPROPERTY()
    int32 TransitionCount = 0;

    // Returns how long (in seconds) the machine has been in the current state.
    float GetTimeInCurrentState(const UWorld* World) const;

    // Returns the previous state tag, or an empty tag if history is empty.
    FGameplayTag GetPreviousStateTag() const;

    void Reset();
};
```

---

# `UStateMachineComponent`

```cpp
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnStateChanged,
    UStateMachineComponent*, Component,
    FGameplayTag, PreviousState,
    FGameplayTag, NewState);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnTransitionBlocked,
    UStateMachineComponent*, Component,
    FGameplayTag, BlockedTargetState);

UCLASS(ClassGroup = (GameCore), meta = (BlueprintSpawnableComponent))
class GAMECORE_API UStateMachineComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    // ── Configuration ─────────────────────────────────────────────────────────

    // The graph definition to run. Assigned at design time.
    UPROPERTY(EditDefaultsOnly, Category = "State Machine")
    TObjectPtr<UStateMachineAsset> StateMachineAsset;

    // Execution and replication authority.
    UPROPERTY(EditDefaultsOnly, Category = "State Machine")
    EStateMachineAuthority AuthorityMode = EStateMachineAuthority::ServerOnly;

    // ── Delegates ─────────────────────────────────────────────────────────────

    // Fires after every successful state transition, on whichever machine(s) run
    // (server, client, or both depending on AuthorityMode).
    UPROPERTY(BlueprintAssignable, Category = "State Machine")
    FOnStateChanged OnStateChanged;

    // Fires when RequestTransition is called but the target state is not reachable
    // from the current state (no valid transition edge exists).
    UPROPERTY(BlueprintAssignable, Category = "State Machine")
    FOnTransitionBlocked OnTransitionBlocked;

    // ── Runtime API ───────────────────────────────────────────────────────────

    // Force a transition to TargetState, bypassing all rules.
    // Safe to call from Blueprint. Respects AuthorityMode — no-ops on wrong authority.
    UFUNCTION(BlueprintCallable, Category = "State Machine")
    void RequestTransition(FGameplayTag TargetStateTag);

    // Evaluate all applicable transition rules from the current state (and any-state rules).
    // Context is forwarded to every UTransitionRule::Evaluate call. May be null.
    // Fires the first passing transition. No-op if no rules pass.
    UFUNCTION(BlueprintCallable, Category = "State Machine")
    void EvaluateTransitions(UObject* Context = nullptr);

    // ── State Queries ─────────────────────────────────────────────────────────

    UFUNCTION(BlueprintPure, Category = "State Machine")
    FGameplayTag GetCurrentStateTag() const;

    UFUNCTION(BlueprintPure, Category = "State Machine")
    bool IsInState(FGameplayTag StateTag) const;

    UFUNCTION(BlueprintPure, Category = "State Machine")
    float GetTimeInCurrentState() const;

    UFUNCTION(BlueprintPure, Category = "State Machine")
    FGameplayTag GetPreviousStateTag() const;

    UFUNCTION(BlueprintPure, Category = "State Machine")
    const UStateMachineAsset* GetAsset() const { return StateMachineAsset; }

    // ── UActorComponent ───────────────────────────────────────────────────────

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    // Replicated current state tag (ServerOnly and Both modes only).
    UPROPERTY(ReplicatedUsing = OnRep_CurrentStateTag)
    FGameplayTag ReplicatedStateTag;

    UFUNCTION()
    void OnRep_CurrentStateTag();

private:
    FStateMachineRuntime Runtime;

    void EnterState(const FGameplayTag& NewStateTag);
    bool CanExecuteLocally() const;
};
```

---

# Replication Contract

| Authority Mode | Server Evaluates | Client Evaluates | What Replicates |
| --- | --- | --- | --- |
| `ServerOnly` | ✅ | ❌ | `ReplicatedStateTag` → clients receive `OnStateChanged` |
| `ClientOnly` | ❌ | ✅ (owning client only) | Nothing |
| `Both` | ✅ (authoritative) | ✅ (predicted) | `ReplicatedStateTag` → client corrects on mismatch |

`RequestTransition` and `EvaluateTransitions` are no-ops when called on the wrong authority side. No RPC is emitted — the owning system is responsible for calling these on the correct machine.

---

# Extension Pattern

Game code and game plugins subclass `UStateNodeBase`, `UTransitionRule`, and optionally `UStateMachineAsset`. GameCore is never modified.

```
GameCore/
  UStateMachineAsset
  UStateNodeBase
  UTransitionRule
  UStateMachineComponent

Game / QuestPlugin/
  UQuestStateMachineAsset  : UStateMachineAsset
  UQuestStateNode          : UStateNodeBase   (+ FQuestStateData USTRUCT payload)
  UQuestTransitionRule     : UTransitionRule
```

`FQuestStateData` is a plain USTRUCT embedded in `UQuestStateNode` as an `EditAnywhere` property. BP-editable in the asset Details panel. Zero overhead — no extra UObject, no asset reference.

---

# Editor Graph

A custom graph editor is provided in the `GameCoreEditor` module:

- **Nodes** — one per `UStateNodeBase` instance. Color-coded by subclass via `GetNodeColor()`. Entry node has a fixed visual indicator.
- **Edges** — one per `FStateTransition`. Label shows `UTransitionRule::GetRuleDescription()` per rule, joined by AND/OR.
- **Details panel** — selecting a node or edge opens its `UStateNodeBase` or `UTransitionRule` properties inline, identical to editing a BT task.
- **Any-state transitions** — displayed in a collapsible sidebar panel, not as graph edges, to avoid visual clutter.
- **PIE highlight** — active state node is highlighted in real time. Read-only during PIE; no edits allowed.
- **Validation** — dangling tags (transitions referencing non-existent states) shown as errors inline on the edge, matching the `IsDataValid` result.

---

# Implementation Constraints

- **`UTransitionRule::Evaluate` must be const and return immediately.** No async, no deferred results, no RPC calls from within.
- **`UStateNodeBase::OnEnter` / `OnExit` must be non-reentrant.** Do not call `RequestTransition` from within `OnEnter` or `OnExit`. Post a deferred call if a follow-on transition is needed.
- **State tags must be unique within an asset.** Enforced by `IsDataValid`. Duplicate tags are an authoring error, not a runtime error.
- **The asset must not be modified at runtime.** It is shared across all components using it. All mutable state lives in `FStateMachineRuntime` on the component.
- **`GrantedTags` on `UStateNodeBase` requires `ITaggedInterface` on the owning actor.** If the interface is absent, tag granting is silently skipped — no crash.
- **`RequestTransition` is a forced move.** It does not validate that a transition edge exists. Use only when the calling system has already verified correctness. `EvaluateTransitions` is the rule-safe path.
- **Blueprint subclasses of `UStateNodeBase` and `UTransitionRule` are supported** but carry a BP VM overhead on every `OnEnter`/`OnExit`/`Evaluate` call. Hot-path nodes (evaluated many times per second) should be C++.
- **`AuthorityMode` is not changed at runtime.** Set it at design time on the component's default properties. Runtime changes have undefined behaviour.

---

# Network Considerations

The component has no RPC methods. It is a pure local evaluator with a single replicated property.

| Concern | Approach |
| --- | --- |
| State replication | `ReplicatedStateTag` is a single `FGameplayTag`. Minimal bandwidth. |
| Client correction (`Both` mode) | `OnRep_CurrentStateTag` calls `EnterState` locally, firing `OnStateChanged` and `OnEnter`/`OnExit` on the client. |
| Transition authority | `RequestTransition` / `EvaluateTransitions` are no-ops on the wrong authority side. The owning system is responsible for calling them in the right context. |
| No transition RPCs | The state machine does not emit RPCs. If the owning system needs to trigger a server-side transition from a client event, it handles that RPC itself and then calls `EvaluateTransitions` server-side. |

---

# Known Limitations

**No cancellation or interrupt priority system.** `RequestTransition` always succeeds if called on the correct authority. There is no concept of a non-interruptible state. Owning systems must guard against unwanted interruptions by checking `IsInState` before calling `RequestTransition`.

**`Both` mode has no rollback.** Client prediction fires `OnEnter`/`OnExit` immediately. If the server transitions to a different state, `OnRep_CurrentStateTag` will fire the correct `OnEnter`/`OnExit` on the client, but any side effects from the mispredicted transition (VFX, sound, animation) are the owning system's responsibility to clean up.

**History is not replicated.** Clients in `ServerOnly` and `Both` modes only receive the current state. If a client needs history (e.g., for UI "previous step" display), it must reconstruct it locally from `OnStateChanged` events.

**Hierarchical FSMs are deferred.** Multiple state machines can communicate via `OnStateChanged` events and Gameplay Tags. Nested execution is not supported in this version.

---

# File and Folder Structure

```cpp
GameCore/
└── Source/
    ├── GameCore/                              ← Runtime module
    │   └── StateMachine/
    │       ├── StateMachineTypes.h            ← EStateMachineAuthority, ETransitionComposition,
    │       │                                     FStateTransition, FStateMachineRuntime
    │       ├── StateMachineAsset.h / .cpp     ← UStateMachineAsset
    │       ├── StateNodeBase.h / .cpp         ← UStateNodeBase
    │       ├── TransitionRule.h / .cpp        ← UTransitionRule
    │       └── StateMachineComponent.h / .cpp ← UStateMachineComponent
    │
    └── GameCoreEditor/                        ← Editor-only module (not in packaged builds)
        └── StateMachine/
            ├── StateMachineGraph.h / .cpp             ← UEdGraph subclass + schema
            ├── StateMachineGraphNode.h / .cpp         ← UEdGraphNode per state
            ├── StateMachineGraphNode_Entry.h / .cpp   ← Fixed entry node
            ├── StateMachineGraphTransition.h / .cpp   ← UEdGraphNode for transition edges
            └── StateMachineEditorToolkit.h / .cpp     ← FAssetEditorToolkit, opens on asset double-click
```

**Key decisions:**

`StateMachineTypes.h` is a single header for all enums and structs — included by the asset, component, and node files. Isolating it avoids cascade recompiles when struct fields change.

`GameCoreEditor` is a separate module. Graph tooling, Slate widgets, and editor-only validation must not be loaded in packaged builds.

No `Public/Private` split. Feature-per-folder is sufficient at this scale.