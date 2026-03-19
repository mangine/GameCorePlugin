# Dialogue System

**Module:** `GameCore` (plugin)  
**Status:** Specification — Pending Implementation  
**UE Version:** 5.7  
**Depends On:** GameCore (Requirement System, Event Bus, Gameplay Tags)

---

## Sub-Pages

| File | Contents |
|---|---|
| [Requirements and Design Decisions](Requirements%20and%20Design%20Decisions.md) | Original requirements, architectural decisions, and rejected alternatives |
| [Data Assets and Node Types](Data%20Assets%20and%20Node%20Types.md) | `UDialogueAsset`, all `UDialogueNode` subclasses, supporting enums and structs |
| [Runtime Structs](Runtime%20Structs.md) | `FDialogueSession`, `FDialogueClientState`, `FDialogueChoice`, `FDialogueVariant` |
| [UDialogueComponent](UDialogueComponent.md) | Server-authoritative NPC component: session management, interpreter loop, RPCs |
| [UDialogueManagerComponent](UDialogueManagerComponent.md) | Per-player component on `APlayerState`: client state relay, choice/ACK RPCs |
| [FDialogueSimulator](FDialogueSimulator.md) | Shared offline interpreter: used by server runtime and editor preview tool |
| [GMS Events](GMS%20Events.md) | All GameplayMessage events emitted by the dialogue system |
| [Editor Preview Tool](Editor%20Preview%20Tool.md) | `SDialoguePreviewWidget`, `FAssetTypeActions_DialogueAsset`, editor module setup |
| [Integration Guide](Integration%20Guide.md) | Setup checklist, wiring from `UInteractionComponent`, UI integration samples |
| [File Structure](File%20Structure.md) | Module layout, Build.cs dependencies |

---

## Design Principles

- **Dialogue assets are content, not code.** `UDialogueAsset` is a `UDataAsset` containing instanced `UDialogueNode` objects. All narrative flow lives in content, not in C++ subclasses per dialogue.
- **The interpreter is stateless; the session owns all mutable state.** `FDialogueSimulator::Step()` takes a `FDialogueSession&` and mutates it. Nodes are pure functions of the session they receive — they own no per-instance state.
- **The server is the sole authority.** The interpreter runs exclusively on the server. Clients receive only what they need to render: speaker, localized text, choices. No node indices, no asset references, no server internals travel to clients.
- **Activation is decoupled.** `UDialogueComponent::StartDialogue` is a plain public method. The component has no knowledge of how it was called. Wiring to `UInteractionComponent::OnInteractionExecuted`, quest events, or any other source is game-code responsibility.
- **Single and Group sessions share one code path.** `StartDialogue` internally wraps the instigator in a one-element participants array and calls the same group path. No branching interpreter logic.
- **Localization is non-negotiable.** Every `FText` in every node is authored as a StringTable reference. Raw string literals in dialogue assets are a hard authoring error.
- **Requirements plug in directly.** Condition nodes and per-choice locks use `URequirement_Composite` from the Requirement System without any bridging layer. Evaluation uses `FRequirementContext` built server-side from the instigator actor.
- **Events are tagged, not coupled.** `UDialogueNode_Event` broadcasts a GMS message keyed by `FGameplayTag`. The dialogue system imports no quest, inventory, cinematic, or game-specific module.
- **Variable scope is session-lifetime only.** Session variables exist in `FDialogueSession::Variables` and are discarded when the session ends. Cross-session memory is a game-side `URequirement` subclass that reads from a persistent store — not the dialogue system's responsibility.
- **The editor preview tool is a first-class deliverable.** `FDialogueSimulator` is shared between the server runtime and the editor tool. No duplication of interpreter logic.

---

## Authority Model

```
Server:
  Owns FDialogueSession.
  Runs FDialogueSimulator::Step() until a node requires external input.
  On UDialogueNode_PlayerChoice: starts optional timeout timer, waits for ServerRPC_SubmitChoice.
  On UDialogueNode_Line (bRequiresAck=true): waits for ServerRPC_AcknowledgeLine.
  On UDialogueNode_Line (bRequiresAck=false): advances immediately.
  Re-validates choice lock conditions on RPC receipt — never trusts client state.
  On Chooser disconnect: calls EndSession(SessionID, EDialogueEndReason::ChooserDisconnected).

Client (Chooser):
  Receives FDialogueClientState via ClientRPC on UDialogueManagerComponent.
  Renders speaker, line text, and choices.
  Counts down timeout locally from TimeoutRemainingSeconds — no per-tick server sync.
  Sends ServerRPC_SubmitChoice(SessionID, ChoiceIndex) for choices.
  Sends ServerRPC_AcknowledgeLine(SessionID) for auto-advancing lines.

Client (Observer, Group sessions only):
  Receives same FDialogueClientState.
  Choice submissions are rejected silently server-side.
  Choices may be shown read-only depending on UDialogueNode_PlayerChoice::bShowChoicesToObservers.
```

---

## Session Lifecycle

```
[Inactive]
  ──(StartDialogue / StartGroupDialogue called)──>
[Running — advancing automatically]
  ──(reaches UDialogueNode_PlayerChoice or bRequiresAck Line)──>
[Waiting — paused for client input]
  ──(ServerRPC_SubmitChoice or ServerRPC_AcknowledgeLine received)──>
[Running]
  ──(reaches UDialogueNode_End or asset stack empty)──>
[Ended — OnDialogueEnded broadcast, session destroyed]

Any state:
  ──(Chooser disconnects)──> [Ended — EDialogueEndReason::ChooserDisconnected]
  ──(ForceEnd called)──>     [Ended — EDialogueEndReason::Interrupted]
```

---

## Quick Usage Guide

### 1. Add components

```cpp
// On the NPC actor (server and clients via replication)
UDialogueComponent* DialogueComp = CreateDefaultSubobject<UDialogueComponent>(TEXT("DialogueComponent"));

// On APlayerState
UDialogueManagerComponent* ManagerComp = CreateDefaultSubobject<UDialogueManagerComponent>(TEXT("DialogueManager"));
```

### 2. Wire activation (game code — example via Interaction System)

```cpp
// In your NPC's BeginPlay or constructor:
UInteractionComponent* InteractionComp = FindComponentByClass<UInteractionComponent>();
if (InteractionComp)
{
    InteractionComp->OnInteractionExecuted.AddDynamic(this, &AMyNPC::OnInteracted);
}

void AMyNPC::OnInteracted(AActor* Instigator, uint8 EntryIndex)
{
    if (UDialogueComponent* DC = FindComponentByClass<UDialogueComponent>())
    {
        DC->StartDialogue(Instigator, GreetingDialogueAsset);
    }
}
```

### 3. Wire group dialogue (game code)

```cpp
void AMyNPC::StartBriefing(const TArray<AActor*>& GroupMembers)
{
    if (UDialogueComponent* DC = FindComponentByClass<UDialogueComponent>())
    {
        // Optional: override Chooser resolution
        DC->ResolveChooser.BindLambda([](const TArray<AActor*>& Participants) -> AActor*
        {
            // Return group leader, or fall back to Participants[0]
            return GetGroupLeader(Participants);
        });
        DC->StartGroupDialogue(GroupMembers, BriefingDialogueAsset);
    }
}
```

### 4. React to dialogue end (game code)

```cpp
// Bind once at BeginPlay
DialogueComp->OnDialogueEnded.AddDynamic(this, &AMyNPC::OnDialogueEnded);

void AMyNPC::OnDialogueEnded(FGuid SessionID, EDialogueEndReason Reason)
{
    // Trigger post-dialogue behaviour: walk away, resume patrol, etc.
}
```

### 5. React to dialogue events (game module — quest wiring example)

```cpp
// In a subsystem or component that bridges dialogue to quests:
UGameplayMessageSubsystem& GMS = UGameplayMessageSubsystem::Get(this);
GMS.RegisterListener(
    TAG_DialogueEvent_QuestUnlocked,
    this,
    &UMyQuestBridge::OnDialogueQuestEvent);
```

### 6. Author a dialogue asset

1. Right-click in Content Browser → **GameCore → Dialogue Asset**.
2. Double-click the asset to open the Dialogue Preview Tool.
3. Add nodes in the graph panel. Connect Start → Line → PlayerChoice → branches.
4. All `FText` fields must reference a StringTable entry — raw strings will trigger a warning on asset save.
5. Use **Context Override** in the preview panel to simulate requirement conditions.
6. Press **Play** to walk through the dialogue interactively without running the game.
