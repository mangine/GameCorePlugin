# State Machine System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The State Machine System is a data-driven, event-driven, graph-based finite state machine framework. It is game-agnostic and designed to be extended by any system — quests, ship states, NPC behaviour, UI flows — without modification to the core.

---

# System Units

| Unit | Class | Role |
| --- | --- | --- |
| Graph Definition | `UStateMachineAsset` | `UDataAsset` owning all state nodes and transitions. One asset per machine type. |
| State Node | `UStateNodeBase` | Instanced `UObject` representing one state. Subclassed per system. |
| Transition Rule | `UTransitionRule` | Instanced `UObject` evaluating a single condition. Subclassed per system. |
| Transition Definition | `FStateTransition` | USTRUCT linking FromState → ToState with a rule and composition mode. |
| Runtime Snapshot | `FStateMachineRuntime` | USTRUCT holding current state, history, and timestamps. Lives on the component. |
| Runtime Driver | `UStateMachineComponent` | Actor component that hosts an asset and drives execution. Implements `IPersistableComponent`. |
| Authority Mode | `EStateMachineAuthority` | Enum declaring server-only, client-only, or replicated execution per component. |

---

# Core Design Principles

- **The asset is the definition, the component is the runtime.** `UStateMachineAsset` carries no mutable state.
- **Event-driven by default, no tick.** Transitions fire only when the owning system calls `RequestTransition` or `EvaluateTransitions`.
- **Authority is explicit.** `EStateMachineAuthority` is set at design time on the component.
- **Only current state replicates.** A single `FGameplayTag` travels over the wire.
- **No cross-system imports at the core layer.** `UStateNodeBase` and `UTransitionRule` have zero dependencies on game systems.
- **Subclassing is the extension mechanism.** No central registry.
- **Persistence is opt-in per component.** `bPersistState` defaults to `true`. Set it `false` on purely cosmetic or ephemeral machines (UI state, VFX flows) that must never write to the persistence system.

---

# How the Pieces Connect

**Authoring.** A designer creates a `UStateMachineAsset`, opens it in the graph editor, adds state nodes, and draws transition edges.

**Runtime setup.** `UStateMachineComponent` is added to an Actor, the asset assigned, `AuthorityMode` set. On `BeginPlay` the component validates the asset, instantiates `FStateMachineRuntime`, enters the entry state, and is ready.

**Driving transitions.** The owning system calls:
- `RequestTransition(TargetStateTag)` — forced move, owning system has already validated.
- `EvaluateTransitions(ContextObject)` — machine checks its rules; first passing rule wins.

**Asset-only evaluation (no component).** Systems that manage their own runtime (e.g. `UQuestComponent` tracking `FQuestRuntime::CurrentStageTag` directly) call `UStateMachineAsset::FindFirstPassingTransition` to evaluate transitions without needing a `UStateMachineComponent`.

**State change.** On transition: `OnExit` → runtime update → `OnEnter` → `OnStateChanged` delegate fires → GMS broadcast fires → `NotifyDirty` called (if `bPersistState`).

**Client reception.** Clients with `AuthorityMode == Both` receive the replicated `FGameplayTag`. `OnStateChanged` fires locally for cosmetics and UI sync.

---

# `EStateMachineAuthority`

```cpp
UENUM(BlueprintType)
enum class EStateMachineAuthority : uint8
{
    ServerOnly,   // Runs server only. State tag replicates to clients.
    ClientOnly,   // Runs owning client only. Nothing replicates.
    Both          // Server authoritative. Client runs predicted copy.
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
    UPROPERTY(EditDefaultsOnly, Instanced, Category = "States")
    TArray<TObjectPtr<UStateNodeBase>> States;

    UPROPERTY(EditDefaultsOnly, Category = "Transitions")
    TArray<FStateTransition> Transitions;

    UPROPERTY(EditDefaultsOnly, Category = "Transitions")
    TArray<FStateTransition> AnyStateTransitions;

    UPROPERTY(EditDefaultsOnly, Category = "States")
    FGameplayTag EntryStateTag;

    UPROPERTY(EditDefaultsOnly, Category = "Settings", meta = (ClampMin = 0, ClampMax = 32))
    int32 HistorySize = 8;

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

    UStateNodeBase* FindNode(const FGameplayTag& StateTag) const;

    // Pure utility for systems that evaluate transitions without a UStateMachineComponent.
    // Returns the ToState tag of the first transition from FromStateTag whose rule passes
    // against ContextObject, or an invalid tag if none pass.
    // AnyState transitions are evaluated first (highest priority), then
    // FromState-specific transitions in definition order.
    // ContextObject is passed through verbatim to UTransitionRule::Evaluate.
    // Pass nullptr for ContextObject when rules do not require context.
    FGameplayTag FindFirstPassingTransition(
        const FGameplayTag& FromStateTag,
        UObject* ContextObject = nullptr) const;
};
```

```cpp
FGameplayTag UStateMachineAsset::FindFirstPassingTransition(
    const FGameplayTag& FromStateTag,
    UObject* ContextObject) const
{
    // AnyState transitions evaluated first — highest priority.
    for (const FStateTransition& T : AnyStateTransitions)
    {
        if (!T.Rule || T.Rule->Evaluate(nullptr, ContextObject))
            return T.ToState;
    }
    for (const FStateTransition& T : Transitions)
    {
        if (T.FromState == FromStateTag
            && (!T.Rule || T.Rule->Evaluate(nullptr, ContextObject)))
            return T.ToState;
    }
    return FGameplayTag(); // No passing transition found
}
```

> **`UStateMachineComponent*` is passed as `nullptr`.** `FindFirstPassingTransition` is used by systems (like `UQuestComponent`) that do not host a `UStateMachineComponent`. Rules that require the component pointer (e.g. time-in-state checks) are not suitable for context-only evaluation and must not be used in quest stage graphs.

---

# `UStateNodeBase`

```cpp
UCLASS(Abstract, EditInlineNew, CollapseCategories, BlueprintType, Blueprintable)
class GAMECORE_API UStateNodeBase : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category = "State")
    FGameplayTag StateTag;

    // No transition may leave this state unless the transition has bCanBypassNonInterruptible.
    UPROPERTY(EditDefaultsOnly, Category = "State")
    bool bNonInterruptible = false;

    UPROPERTY(EditDefaultsOnly, Category = "State")
    FGameplayTagContainer GrantedTags;

    virtual void OnEnter(UStateMachineComponent* Component) {}
    virtual void OnExit(UStateMachineComponent* Component)  {}

#if WITH_EDITOR
    virtual FString GetNodeDescription() const { return FString(); }
    virtual FLinearColor GetNodeColor()   const { return FLinearColor::White; }
#endif
};
```

---

# `FStateTransition`

```cpp
USTRUCT()
struct GAMECORE_API FStateTransition
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly) FGameplayTag FromState;
    UPROPERTY(EditDefaultsOnly) FGameplayTag ToState;
    UPROPERTY(EditDefaultsOnly, Instanced) TObjectPtr<UTransitionRule> Rule;
    UPROPERTY(EditDefaultsOnly) ETransitionComposition Composition = ETransitionComposition::And;

    // If true, this transition may fire even when current state is bNonInterruptible.
    // Reserved for hard resets (AnyState → Destroyed).
    UPROPERTY(EditDefaultsOnly) bool bCanBypassNonInterruptible = false;

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

    UPROPERTY() FGameplayTag CurrentStateTag;
    UPROPERTY() TArray<FGameplayTag> History;
    UPROPERTY() float CurrentStateEnterTime = 0.f;
    UPROPERTY() int32 TransitionCount = 0;

    float        GetTimeInCurrentState(const UWorld* World) const;
    FGameplayTag GetPreviousStateTag() const;
    void         Reset();
};
```

---

# `UStateMachineComponent`

`UStateMachineComponent` implements `IPersistableComponent`. The owning actor must have a `UPersistenceRegistrationComponent` for persistence to be active. If no registration component is present, `NotifyDirty` silently no-ops — the machine functions normally, it simply does not persist.

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
class GAMECORE_API UStateMachineComponent : public UActorComponent, public IPersistableComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category = "State Machine")
    TObjectPtr<UStateMachineAsset> StateMachineAsset;

    UPROPERTY(EditDefaultsOnly, Category = "State Machine")
    EStateMachineAuthority AuthorityMode = EStateMachineAuthority::ServerOnly;

    // Controls whether this component participates in the persistence system.
    // Set false for purely cosmetic or ephemeral machines (UI, VFX) that must
    // never write to the save queue. Ignored if the owning actor has no
    // UPersistenceRegistrationComponent.
    UPROPERTY(EditDefaultsOnly, Category = "State Machine|Persistence")
    bool bPersistState = true;

    // Optional stable name used as the persistence blob key.
    // Required only when an actor hosts more than one UStateMachineComponent.
    // Must never be renamed after shipping — it is the blob identifier.
    // When left as NAME_None, defaults to "StateMachine".
    UPROPERTY(EditDefaultsOnly, Category = "State Machine|Persistence")
    FName PersistenceKeyOverride = NAME_None;

    // ── Delegates — intra-system / Blueprint convenience ──────────────────────

    // Fires after every successful state transition.
    // Decoupled external systems MUST use GMS channel GameCoreEvent.StateMachine.StateChanged.
    UPROPERTY(BlueprintAssignable, Category = "State Machine")
    FOnStateChanged OnStateChanged;

    // Fires when a transition is blocked by bNonInterruptible.
    // External consumers may use GMS channel GameCoreEvent.StateMachine.TransitionBlocked.
    UPROPERTY(BlueprintAssignable, Category = "State Machine")
    FOnTransitionBlocked OnTransitionBlocked;

    // ── Runtime API ───────────────────────────────────────────────────────────

    void ForceTransition(const FGameplayTag& TargetStateTag);
    void RequestTransition(const FGameplayTag& TargetStateTag);
    void EvaluateTransitions(UObject* ContextObject = nullptr);

    bool IsInState(const FGameplayTag& StateTag) const;
    const FStateMachineRuntime& GetRuntime() const { return Runtime; }

    UPROPERTY(ReplicatedUsing = OnRep_CurrentStateTag)
    FGameplayTag ReplicatedStateTag;

    UFUNCTION()
    void OnRep_CurrentStateTag();

    // ── IPersistableComponent ─────────────────────────────────────────────────

    virtual FName    GetPersistenceKey()    const override;
    virtual uint32   GetSchemaVersion()     const override { return 1; }
    virtual void     Serialize_Save(FArchive& Ar) override;
    virtual void     Serialize_Load(FArchive& Ar, uint32 SavedVersion) override;
    // Migrate() is no-op at v1. History array growth is the most likely future migration target.

private:
    FStateMachineRuntime Runtime;

    void EnterState(const FGameplayTag& NewStateTag);
    // RestoreState re-applies a persisted state tag without firing OnExit/OnEnter/delegates/GMS.
    // Used exclusively by Serialize_Load. Not a transition — no side effects.
    void RestoreState(const FGameplayTag& SavedStateTag);
    bool CanExecuteLocally() const;
    // Pure predicate — no side effects. Returns true if the transition to TargetStateTag is
    // permitted given the current non-interruptible state. Blocked-transition events are fired
    // by the caller (RequestTransition / EvaluateTransitions), never inside this function.
    bool IsTransitionPermitted(const FGameplayTag& TargetStateTag) const;
};
```

---

## EnterState — GMS Broadcast

```cpp
void UStateMachineComponent::EnterState(const FGameplayTag& NewStateTag)
{
    const FGameplayTag PreviousState = Runtime.CurrentStateTag;

    if (UStateNodeBase* OldNode = StateMachineAsset->FindNode(PreviousState))
        OldNode->OnExit(this);

    Runtime.CurrentStateTag = NewStateTag;
    if (StateMachineAsset->HistorySize > 0)
        Runtime.History.Insert(PreviousState, 0);
    Runtime.CurrentStateEnterTime = GetWorld()->GetTimeSeconds();
    Runtime.TransitionCount++;

    if (UStateNodeBase* NewNode = StateMachineAsset->FindNode(NewStateTag))
        NewNode->OnEnter(this);

    // 1. Delegate — direct Blueprint bindings and intra-actor wiring.
    OnStateChanged.Broadcast(this, PreviousState, NewStateTag);

    // 2. Event Bus — all decoupled external systems listen here.
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        FStateMachineStateChangedMessage Msg;
        Msg.Component         = this;
        Msg.OwnerActor        = GetOwner();
        Msg.PreviousState     = PreviousState;
        Msg.NewState          = NewStateTag;
        Msg.StateMachineAsset = StateMachineAsset;
        Bus->Broadcast(GameCoreEventTags::StateMachine_StateChanged, Msg,
            EGameCoreEventScope::Both);
    }

    // 3. Persistence dirty mark — only on server, only when opted in.
    if (bPersistState && GetOwner()->HasAuthority())
        NotifyDirty(this);
}
```

## IsTransitionPermitted — Pure Predicate

`IsTransitionPermitted` is a **pure predicate**. It evaluates the non-interruptible guard and returns a `bool`. It has no side effects.

Blocked-transition events (`OnTransitionBlocked` delegate and `GameCoreEvent.StateMachine.TransitionBlocked` GMS broadcast) are fired by the **call site** (`RequestTransition` / `EvaluateTransitions`) after this function returns `false`.

```cpp
bool UStateMachineComponent::IsTransitionPermitted(const FGameplayTag& TargetStateTag) const
{
    const UStateNodeBase* CurrentNode = StateMachineAsset->FindNode(Runtime.CurrentStateTag);
    if (!CurrentNode || !CurrentNode->bNonInterruptible)
        return true;

    for (const FStateTransition& T : StateMachineAsset->Transitions)
    {
        if (T.FromState == Runtime.CurrentStateTag
            && T.ToState == TargetStateTag
            && T.bCanBypassNonInterruptible)
            return true;
    }

    return false;
}
```

## RequestTransition / EvaluateTransitions — Blocked Event Fire Site

```cpp
void UStateMachineComponent::RequestTransition(const FGameplayTag& TargetStateTag)
{
    if (!CanExecuteLocally()) return;

    if (!IsTransitionPermitted(TargetStateTag))
    {
        // Fire blocked events here — never inside IsTransitionPermitted.
        OnTransitionBlocked.Broadcast(this, TargetStateTag);

        if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
        {
            FStateMachineTransitionBlockedMessage Msg;
            Msg.Component          = this;
            Msg.OwnerActor         = GetOwner();
            Msg.BlockedTargetState = TargetStateTag;
            Msg.CurrentState       = Runtime.CurrentStateTag;
            Bus->Broadcast(GameCoreEventTags::StateMachine_TransitionBlocked, Msg,
                EGameCoreEventScope::Both);
        }
        return;
    }

    EnterState(TargetStateTag);
}
```

> `EvaluateTransitions` follows the same pattern: call `IsTransitionPermitted` on the candidate, fire blocked events at the call site on `false`, call `EnterState` on `true`.

---

## Persistence — Serialization Contract

`UStateMachineComponent` implements `IPersistableComponent`. The component serializes the full `FStateMachineRuntime` snapshot: current state, history, enter time, and transition count.

**Schema version: 1**

```cpp
FName UStateMachineComponent::GetPersistenceKey() const
{
    if (PersistenceKeyOverride != NAME_None)
        return PersistenceKeyOverride;
    return FName("StateMachine");
}

void UStateMachineComponent::Serialize_Save(FArchive& Ar)
{
    // Ar is in write mode. Runtime must not be mutated here.
    Ar << Runtime.CurrentStateTag;

    int32 HistoryCount = FMath::Min(Runtime.History.Num(), StateMachineAsset->HistorySize);
    Ar << HistoryCount;
    for (int32 i = 0; i < HistoryCount; ++i)
        Ar << Runtime.History[i];

    Ar << Runtime.CurrentStateEnterTime;
    Ar << Runtime.TransitionCount;
}

void UStateMachineComponent::Serialize_Load(FArchive& Ar, uint32 SavedVersion)
{
    FGameplayTag SavedStateTag;
    Ar >> SavedStateTag;

    int32 HistoryCount = 0;
    Ar >> HistoryCount;
    TArray<FGameplayTag> SavedHistory;
    SavedHistory.SetNum(HistoryCount);
    for (int32 i = 0; i < HistoryCount; ++i)
        Ar >> SavedHistory[i];

    float SavedEnterTime   = 0.f;
    int32 SavedTransitions = 0;
    Ar >> SavedEnterTime;
    Ar >> SavedTransitions;

    // Validate the saved state tag still exists in the asset.
    // If content was updated and the tag was removed, fall back to entry state
    // and log a warning so designers are alerted during testing.
    if (StateMachineAsset->FindNode(SavedStateTag))
    {
        RestoreState(SavedStateTag);
    }
    else
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("UStateMachineComponent [%s]: Saved state tag '%s' no longer exists in asset '%s'. "
                 "Falling back to entry state."),
            *GetOwner()->GetName(),
            *SavedStateTag.ToString(),
            *StateMachineAsset->GetName());
        RestoreState(StateMachineAsset->EntryStateTag);
    }

    // Restore metadata regardless of state tag validity.
    Runtime.History           = MoveTemp(SavedHistory);
    Runtime.CurrentStateEnterTime = SavedEnterTime;
    Runtime.TransitionCount   = SavedTransitions;
}
```

### RestoreState

`RestoreState` applies a state tag directly to `Runtime.CurrentStateTag` without triggering `OnExit`, `OnEnter`, delegates, GMS broadcasts, or `NotifyDirty`. It is the restore path — not a transition.

```cpp
void UStateMachineComponent::RestoreState(const FGameplayTag& SavedStateTag)
{
    Runtime.CurrentStateTag = SavedStateTag;
    // No OnExit / OnEnter. No delegates. No GMS. No dirty mark.
    // The owning system is responsible for any post-load side-effect
    // reconciliation if the game requires it (e.g. re-applying GrantedTags).
}
```

> **Post-load reconciliation note.** If a state grants tags or activates abilities (via `GrantedTags` on `UStateNodeBase`), the owning system must re-apply those grants after loading. `RestoreState` does not replay `OnEnter`. Document this contract in any game-layer extension that uses `GrantedTags`.

---

# Replication Contract

| Authority Mode | Server Evaluates | Client Evaluates | What Replicates |
| --- | --- | --- | --- |
| `ServerOnly` | ✅ | ❌ | `ReplicatedStateTag` → clients receive `OnStateChanged` + GMS broadcast |
| `ClientOnly` | ❌ | ✅ (owning client) | Nothing |
| `Both` | ✅ (authoritative) | ✅ (predicted) | `ReplicatedStateTag` → client corrects on mismatch |

---

# Extension Pattern

```
GameCore/
  UStateMachineAsset        — FindFirstPassingTransition for asset-only evaluation
  UStateNodeBase
  UTransitionRule
  UStateMachineComponent

GameCore Quest module/
  UQuestStateNode           : UStateNodeBase
    + bIsCompletionState      — UQuestComponent triggers complete flow on enter
    + bIsFailureState         — UQuestComponent triggers fail flow on enter
    + OnEnter / OnExit        — no-op (quest side effects driven by UQuestComponent)

  UQuestTransitionRule      : UTransitionRule
    + Requirements: URequirementList
    + Evaluate(Component, ContextObject):
        Cast ContextObject to FRequirementContext*
        Return Requirements->Evaluate(*Ctx).bPassed
```

## `UQuestStateNode` and `UQuestTransitionRule`

These are defined in the GameCore Quest module (`GameCore/Source/GameCore/Quest/`). See `GameCore Changes.md` in the Quest System specification for full class declarations.

**Authoring a branching quest stage graph:**
1. Create a `UStateMachineAsset` for the quest.
2. Add `UQuestStateNode` instances for each stage — mark terminal nodes with `bIsCompletionState` or `bIsFailureState`.
3. Draw transitions between nodes. Set `Rule` to a `UQuestTransitionRule` instance and author `Requirements` in the Details panel.
4. Multiple outgoing transitions from the same stage express branching — `FindFirstPassingTransition` picks the first passing rule.
5. Assign the asset to `UQuestDefinition::StageGraph`.

---

# Known Limitations

- **Non-interruptible state contract.** `bNonInterruptible` blocks all transitions except those with `bCanBypassNonInterruptible`.
- **`Both` mode misprediction rollback.** `OnRep_CurrentStateTag` fires `EnterState` with the correct server state. Owning system is responsible for all side-effect cleanup in `OnExit`.
- **History is not replicated.** Clients reconstruct from `OnStateChanged` / GMS events.
- **Hierarchical FSMs deferred.** Multiple machines communicate via GMS state-changed events and Gameplay Tags.
- **Post-load `GrantedTags` reconciliation is caller responsibility.** `RestoreState` does not replay `OnEnter`. Any state that grants tags or activates abilities must be re-applied by the owning system after loading. See Persistence section above.
- **`FindFirstPassingTransition` passes `nullptr` as `UStateMachineComponent*`.** Rules that require the component pointer are not compatible with asset-only evaluation contexts such as quest stage graphs.

---

# File and Folder Structure

```
GameCore/
└── Source/
    ├── GameCore/
    │   └── StateMachine/
    │       ├── StateMachineTypes.h
    │       ├── StateMachineAsset.h / .cpp
    │       ├── StateNodeBase.h / .cpp
    │       ├── TransitionRule.h / .cpp
    │       └── StateMachineComponent.h / .cpp
    └── GameCoreEditor/
        └── StateMachine/
            ├── StateMachineGraph.h / .cpp
            ├── StateMachineGraphNode.h / .cpp
            ├── StateMachineGraphNode_Entry.h / .cpp
            ├── StateMachineGraphTransition.h / .cpp
            └── StateMachineEditorToolkit.h / .cpp
```

---

# Implementation Constraints

- **`UTransitionRule::Evaluate` must be const and return immediately.**
- **`IsTransitionPermitted` is a pure predicate — no side effects.** All blocked-transition events (`OnTransitionBlocked` delegate and GMS broadcast) must be fired at the `RequestTransition` / `EvaluateTransitions` call site after the permission check returns `false`, never inside the predicate itself.
- **`UStateNodeBase::OnEnter` / `OnExit` must be non-reentrant.** Do not call `RequestTransition` from within them — post a deferred call.
- **State tags must be unique within an asset.**
- **The asset must not be modified at runtime.**
- **`RequestTransition` is a forced move** — use only when the caller has verified correctness.
- **`AuthorityMode` is not changed at runtime.**
- **`GetPersistenceKey()` must never be renamed after shipping** — it is the blob identifier in saved payloads. If an actor hosts multiple `UStateMachineComponent` instances, each must have a unique `PersistenceKeyOverride` set.
- **`Serialize_Save` must be strictly read-only** — no state mutation during serialization.
- **`NotifyDirty` is called only on the server** (`HasAuthority()`) inside `EnterState`, gated by `bPersistState`.
- **`FindFirstPassingTransition` is pure** — no state mutation, safe to call from const contexts.
