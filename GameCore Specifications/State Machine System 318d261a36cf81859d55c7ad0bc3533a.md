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

---

# Core Design Principles

- **The asset is the definition, the component is the runtime.** `UStateMachineAsset` carries no mutable state.
- **Event-driven by default, no tick.** Transitions fire only when the owning system calls `RequestTransition` or `EvaluateTransitions`.
- **Authority is explicit.** `EStateMachineAuthority` is set at design time on the component.
- **Only current state replicates.** A single `FGameplayTag` travels over the wire.
- **No cross-system imports at the core layer.** `UStateNodeBase` and `UTransitionRule` have zero dependencies on game systems.
- **Subclassing is the extension mechanism.** No central registry.

---

# How the Pieces Connect

**Authoring.** A designer creates a `UStateMachineAsset` in the Content Browser, opens it in the custom graph editor, adds state nodes, and draws transition edges. An Entry node marks the initial state.

**Runtime setup.** `UStateMachineComponent` is added to an Actor, the asset is assigned, and `AuthorityMode` is set. On `BeginPlay`, the component validates the asset, instantiates `FStateMachineRuntime`, enters the entry state, and is ready.

**Driving transitions.** The owning system calls:
- `RequestTransition(TargetStateTag)` — forced move when the owning system has already validated the target.
- `EvaluateTransitions(ContextObject)` — machine checks its rules; first passing rule wins.

**State change.** On transition: `OnExit` → `FStateMachineRuntime` update → `OnEnter` → `OnStateChanged` delegate fires → GMS broadcast fires.

**Client reception.** Clients with `AuthorityMode == Both` receive the replicated `FGameplayTag`. `OnStateChanged` fires locally, allowing cosmetic updates and UI sync.

---

# `EStateMachineAuthority`

```cpp
UENUM(BlueprintType)
enum class EStateMachineAuthority : uint8
{
    ServerOnly,   // Machine runs server only. State tag replicates to clients.
    ClientOnly,   // Machine runs owning client only. Nothing replicates.
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
    UPROPERTY(EditDefaultsOnly, Category = "State")
    FGameplayTag StateTag;

    // If true, no transition may leave this state unless bCanBypassNonInterruptible is set
    // on the transition. Reserved for hard-reset transitions (e.g. AnyState → Destroyed).
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

    UPROPERTY(EditDefaultsOnly, Category = "Transition")
    FGameplayTag FromState;

    UPROPERTY(EditDefaultsOnly, Category = "Transition")
    FGameplayTag ToState;

    UPROPERTY(EditDefaultsOnly, Instanced, Category = "Transition")
    TObjectPtr<UTransitionRule> Rule;

    UPROPERTY(EditDefaultsOnly, Category = "Transition")
    ETransitionComposition Composition = ETransitionComposition::And;

    // If true, this transition may fire even when the current state is bNonInterruptible.
    UPROPERTY(EditDefaultsOnly, Category = "Transition")
    bool bCanBypassNonInterruptible = false;

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

    float GetTimeInCurrentState(const UWorld* World) const;
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
    UPROPERTY(EditDefaultsOnly, Category = "State Machine")
    TObjectPtr<UStateMachineAsset> StateMachineAsset;

    UPROPERTY(EditDefaultsOnly, Category = "State Machine")
    EStateMachineAuthority AuthorityMode = EStateMachineAuthority::ServerOnly;

    // ── Delegates  (intra-system / Blueprint convenience) ─────────────────────

    // Fires after every successful state transition on whichever machine(s) run.
    // External systems that are decoupled from this component must use
    // GMS channel GameCoreEvent.StateMachine.StateChanged instead.
    UPROPERTY(BlueprintAssignable, Category = "State Machine")
    FOnStateChanged OnStateChanged;

    // Fires when a transition is blocked by bNonInterruptible.
    // External debug/AI consumers may use GMS channel GameCoreEvent.StateMachine.TransitionBlocked.
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

private:
    FStateMachineRuntime Runtime;

    void EnterState(const FGameplayTag& NewStateTag);
    bool CanExecuteLocally() const;
    bool IsTransitionPermitted(const FGameplayTag& TargetStateTag) const;
};
```

---

## EnterState — GMS Broadcast

```cpp
void UStateMachineComponent::EnterState(const FGameplayTag& NewStateTag)
{
    const FGameplayTag PreviousState = Runtime.CurrentStateTag;

    // Exit previous state
    if (UStateNodeBase* OldNode = StateMachineAsset->FindNode(PreviousState))
        OldNode->OnExit(this);

    // Update runtime
    Runtime.CurrentStateTag = NewStateTag;
    if (StateMachineAsset->HistorySize > 0)
        Runtime.History.Insert(PreviousState, 0);
    Runtime.CurrentStateEnterTime = GetWorld()->GetTimeSeconds();
    Runtime.TransitionCount++;

    // Enter new state
    if (UStateNodeBase* NewNode = StateMachineAsset->FindNode(NewStateTag))
        NewNode->OnEnter(this);

    // 1. Delegate — for direct Blueprint bindings and intra-actor wiring.
    OnStateChanged.Broadcast(this, PreviousState, NewStateTag);

    // 2. GMS broadcast — for all decoupled external systems.
    if (UGameCoreEventSubsystem* EventBus = GetWorld()->GetSubsystem<UGameCoreEventSubsystem>())
    {
        FStateMachineStateChangedMessage Msg;
        Msg.Component      = this;
        Msg.OwnerActor     = GetOwner();
        Msg.PreviousState  = PreviousState;
        Msg.NewState       = NewStateTag;
        Msg.StateMachineAsset = StateMachineAsset;
        EventBus->Broadcast(GameCoreEventTags::StateMachine_StateChanged, Msg);
    }
}
```

## IsTransitionPermitted — GMS Broadcast on Block

```cpp
bool UStateMachineComponent::IsTransitionPermitted(const FGameplayTag& TargetStateTag) const
{
    const UStateNodeBase* CurrentNode = StateMachineAsset->FindNode(Runtime.CurrentStateTag);
    if (!CurrentNode || !CurrentNode->bNonInterruptible)
        return true;

    // Check if any transition to this target has bypass flag
    for (const FStateTransition& T : StateMachineAsset->Transitions)
    {
        if (T.FromState == Runtime.CurrentStateTag && T.ToState == TargetStateTag && T.bCanBypassNonInterruptible)
            return true;
    }

    // Blocked — broadcast
    OnTransitionBlocked.Broadcast(const_cast<UStateMachineComponent*>(this), TargetStateTag);

    if (UGameCoreEventSubsystem* EventBus = GetWorld()->GetSubsystem<UGameCoreEventSubsystem>())
    {
        FStateMachineTransitionBlockedMessage Msg;
        Msg.Component          = const_cast<UStateMachineComponent*>(this);
        Msg.OwnerActor         = GetOwner();
        Msg.BlockedTargetState = TargetStateTag;
        Msg.CurrentState       = Runtime.CurrentStateTag;
        EventBus->Broadcast(GameCoreEventTags::StateMachine_TransitionBlocked, Msg);
    }

    return false;
}
```

---

# Replication Contract

| Authority Mode | Server Evaluates | Client Evaluates | What Replicates |
| --- | --- | --- | --- |
| `ServerOnly` | ✅ | ❌ | `ReplicatedStateTag` → clients receive `OnStateChanged` and GMS broadcast |
| `ClientOnly` | ❌ | ✅ (owning client only) | Nothing |
| `Both` | ✅ (authoritative) | ✅ (predicted) | `ReplicatedStateTag` → client corrects on mismatch |

---

# Extension Pattern

```
GameCore/
  UStateMachineAsset
  UStateNodeBase
  UTransitionRule
  UStateMachineComponent

Game / QuestPlugin/
  UQuestStateMachineAsset  : UStateMachineAsset
  UQuestStateNode          : UStateNodeBase
  UQuestTransitionRule     : UTransitionRule
```

---

# Known Limitations

- **Non-interruptible state contract.** `bNonInterruptible = true` blocks all transitions except those with `bCanBypassNonInterruptible = true`.
- **`Both` mode misprediction rollback.** `OnRep_CurrentStateTag` fires `EnterState` with the correct server state. The owning system is responsible for all side-effect cleanup in `OnExit`.
- **History is not replicated.** Clients reconstruct from `OnStateChanged` / GMS events.
- **Hierarchical FSMs deferred.** Multiple machines communicate via GMS state-changed events and Gameplay Tags.

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
- **`UStateNodeBase::OnEnter` / `OnExit` must be non-reentrant.** Do not call `RequestTransition` from within them.
- **State tags must be unique within an asset.**
- **The asset must not be modified at runtime.**
- **`RequestTransition` is a forced move** — use only when the caller has verified correctness.
- **`AuthorityMode` is not changed at runtime.**
