# Dialogue System — Architecture

**Module:** `GameCore` (plugin) + `GameCoreEditor` (editor-only)  
**Status:** Specification — Pending Implementation  
**UE Version:** 5.7

---

## Dependencies

### Unreal Engine Modules
| Module | Why |
|---|---|
| `Engine` | `UActorComponent`, `UDataAsset`, `UObject`, `UWorld`, `APlayerState` |
| `GameplayTags` | `FGameplayTag`, `FGameplayTagContainer` — speaker identity, event routing, end-reason tags |
| `GameplayMessageSubsystem` | GMS broadcast in `UDialogueNode_Event`; listener registration in game code |
| `SlateCore`, `Slate` | Editor preview widget (`SDialoguePreviewWidget`) — editor module only |
| `UnrealEd`, `AssetTools` | `FAssetTypeActions_DialogueAsset`, asset editor registration — editor module only |

### GameCore Plugin Systems
| System | How used |
|---|---|
| **Requirement System** | `URequirement_Composite` used directly in `UDialogueNode_Condition` and per-choice lock conditions. `FRequirementContext` built server-side by `FDialogueSimulator::BuildContext()`. No bridging layer. |
| **Backend (Logging)** | All server-side warnings and errors routed through `FGameCoreBackend::GetLogging(TAG_Log_Dialogue)`. Raw `UE_LOG` only in editor-only paths where the backend is unavailable. |
| **Event Bus (GMS)** | `UDialogueNode_Event` uses `UGameplayMessageSubsystem::BroadcastMessage`. Session lifecycle events (`Session.Started`, `Session.Ended`) also broadcast via GMS. |

### No Dependency On
- Interaction System — activation is game-code responsibility, not the dialogue system's.
- Serialization / Persistence — session variables are ephemeral by design.
- State Machine — separate data model and lifecycle (see rejected alternatives).
- Any game-specific module (quest, inventory, cinematic, faction).

---

## Requirements

| # | Requirement |
|---|---|
| R1 | Dialogue flow must be authorable by designers without writing C++ or Blueprint. |
| R2 | Dialogue must support branching, converging paths, and reusable sub-dialogues (tunneling). |
| R3 | All conditions and choice locks must reuse the Requirement System — no parallel condition framework. |
| R4 | Dialogue must trigger game-side systems without importing those modules. |
| R5 | All line text must participate in the UE localization pipeline (StringTable references). |
| R6 | The server is the sole authority — dialogue state is never client-authoritative. |
| R7 | Single-player dialogue is the default, low-overhead case. |
| R8 | Group dialogue (multiple participants, one Chooser) must be supported. |
| R9 | An editor preview tool allows walkthroughs without launching the game. |
| R10 | The activation mechanism is fully decoupled — game code wires `StartDialogue` to whatever event it needs. |

---

## Features

- **Branching / converging graph** via flat node array with integer edge indices.
- **Sub-dialogue tunneling** — `UDialogueNode_SubDialogue` pushes a new asset onto `FDialogueSession::AssetStack`; natural end of the sub-asset pops back to the parent.
- **Timed choices** — `UDialogueNode_PlayerChoice::TimeoutSeconds`; server auto-submits the default choice on expiry through the same validated path as a player choice.
- **Observer read-only mode** — Group session non-Choosers receive `FDialogueClientState` with `bIsObserver=true`; choice submissions are silently rejected server-side.
- **Chooser resolution delegate** — `UDialogueComponent::ResolveChooser` defaults to `Participants[0]`; game code overrides it for group-leader policies.
- **ACK-controlled vs auto-advancing lines** — `UDialogueNode_Line::bRequiresAck` (default `true`); `false` for cutscene narration lines that should advance without player input.
- **Session variables** — `FName → FDialogueVariant` (bool, int32, FGameplayTag) scoped to session lifetime; injected into `FRequirementContext` for condition evaluation.
- **GMS event nodes** — `UDialogueNode_Event` broadcasts a tagged GMS message; game code listens without the dialogue system knowing those systems exist.
- **Infinite loop guard** — `FDialogueSimulator::MaxAutoSteps = 256` consecutive `Continue` actions abort with `AssetError`.
- **Anti-cheat choice validation** — `ResolveChoice` re-evaluates the lock condition server-side on every RPC receipt regardless of client-reported state.
- **Editor preview tool** — `SDialoguePreviewWidget` drives `FDialogueSimulator` offline; no PIE, no world, no network.
- **Localization enforcement** — `UDialogueAsset::IsDataValid` warns on save for any `FText` not sourced from a StringTable.

---

## Design Decisions

### Custom Asset + Interpreter, not Inkpot
Ink compiles dialogue strings into JSON with no native `FText`/StringTable pipeline, its runtime is stateful and single-client–oriented, and `EXTERNAL` function bridging for Requirement System and GMS is verbose and untypesafe. A custom `UDialogueAsset` + `FDialogueSimulator` gives full control over localization, replication, requirements integration, and editor tooling.

### Flat Node Array with Integer Indices
Nodes in `UDialogueAsset` are a `TArray<TObjectPtr<UDialogueNode>>`. Edges are `int32` indices. No separate edge objects, no graph topology class. Simpler serialization, trivial deep-copy for the simulator, index stability guaranteed after cook. Indices are internal — never sent over the wire.

### `FDialogueSimulator` is a Plain Struct
The interpreter is a value type with a static `Advance()` method. No UObject lifecycle, no tick, no world reference. Both `UDialogueComponent` (server runtime) and `SDialoguePreviewWidget` (editor) call `Advance()` directly — zero duplication of interpreter logic.

### Session Variables are Ephemeral
Cross-session memory belongs in a game-side persistent store queried by a custom `URequirement` subclass. The dialogue system is not a persistence layer.

### Chooser Disconnect Ends Session
Promoting a new Chooser mid-session is ambiguous (did the departed Chooser see the current line? which choices were valid for them?). Ending with `EDialogueEndReason::ChooserDisconnected` is clean and unambiguous.

### No Per-Tick Timeout Replication
`TimeoutRemainingSeconds` is sent once when the choice state is pushed. The client counts down locally. One second of drift is acceptable for a countdown UI and avoids per-tick RPC overhead. The server is always authoritative — a late client RPC after server expiry is silently ignored.

### `UDialogueComponent` Receives Choices as Direct Method Calls
`UDialogueManagerComponent` sends `ServerRPC_SubmitChoice` / `ServerRPC_AcknowledgeLine` to the server, then the server-side implementation calls `UDialogueComponent::Server_ReceiveChoice` / `Server_ReceiveACK` directly as C++ method calls. This avoids a second RPC hop and keeps `UDialogueComponent` interface cleaner.

### Chooser Identity Carried in `FDialogueClientState`
`FDialogueClientState::OwnerComponent` carries the `UDialogueComponent` reference. The manager component populates `SessionOwners` automatically on `ClientRPC_ReceiveDialogueState` — no separate registration RPC needed.

---

## Logic Flow

### Session Lifecycle
```
[Inactive]
  ──(StartDialogue / StartGroupDialogue)──>
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

### Class Interaction — Single Dialogue
```
Game Code
  └─ UDialogueComponent::StartDialogue(Instigator, Asset)
       └─ StartGroupDialogue([Instigator], Asset)  ← single path wraps instigator
            └─ Creates FDialogueSession, adds to ActiveSessions
            └─ RunSession(Session)
                 └─ FDialogueSimulator::Advance(Session)
                      └─ Loop: ExecuteNode → FDialogueStepResult
                           [Continue]        → advance CurrentNodeIndex, loop
                           [SubDialoguePush] → push FDialogueStackFrame, loop
                           [WaitForACK/Choice] → return to RunSession
                           [EndSession]      → return to RunSession
                 └─ PushClientState(Session, Result)
                      └─ GetManagerComponent(Participant)
                           └─ UDialogueManagerComponent::ClientRPC_ReceiveDialogueState(State)
                                └─ OnDialogueStateReceived.Broadcast(State)  ← UI binds here

UI Widget
  └─ UDialogueManagerComponent::SubmitChoice(SessionID, ChoiceIndex)
       └─ ServerRPC_SubmitChoice(SessionID, ChoiceIndex, TargetComponent)
            └─ UDialogueComponent::Server_ReceiveChoice(SessionID, Sender, ChoiceIndex)
                 └─ FDialogueSimulator::ResolveChoice(Session, ChoiceIndex)  ← re-validates lock
                 └─ Session.CurrentFrame().CurrentNodeIndex = TargetNode
                 └─ RunSession(Session)  ← resumes
```

### `FDialogueSimulator::Advance` — Internal Loop
```
while Steps < MaxAutoSteps:
  result = ExecuteNode(Session)
  switch result.Action:
    Continue        → CurrentNodeIndex = result.NextNode; continue loop
    SubDialoguePush → store ReturnNodeIndex on current frame, push new FDialogueStackFrame; continue loop
    WaitForACK      → Session.bWaiting = true; return result  (CurrentNodeIndex NOT advanced)
    WaitForChoice   → Session.bWaiting = true; return result  (CurrentNodeIndex NOT advanced)
    EndSession      → return result
  if NextNode == INDEX_NONE and stack depth > 1: PopStack, continue loop
  if NextNode == INDEX_NONE and root frame: return EndSession/Completed
if Steps >= MaxAutoSteps: return EndSession/AssetError
```

> **Critical invariant:** `CurrentNodeIndex` always points at the *currently executing or currently waiting* node — never the next. The simulator advances it to `result.NextNode` only on `Continue`. On `WaitForACK` / `WaitForChoice` it stays at the waiting node so `Server_ReceiveACK` and `ResolveChoice` can read the waiting node directly without any secondary index storage.

### Editor Preview — No World Required
```
SDialoguePreviewWidget
  └─ FDialoguePreviewContext::Restart()
       └─ Constructs FDialogueSession with UDialogueEditorChooserProxy as Chooser
       └─ Calls FDialogueSimulator::Advance(Session)  ← same code path as server
  └─ FDialoguePreviewContext::SubmitChoice(int32)
       └─ FDialogueSimulator::ResolveChoice(Session, ChoiceIndex)
       └─ FDialogueSimulator::Advance(Session)
```

---

## Authority Model

```
Server:
  Owns FDialogueSession.
  Runs FDialogueSimulator::Advance() until a node requires external input.
  On UDialogueNode_PlayerChoice: starts optional timeout timer, waits for Server_ReceiveChoice.
  On UDialogueNode_Line (bRequiresAck=true): waits for Server_ReceiveACK.
  On UDialogueNode_Line (bRequiresAck=false): advances immediately.
  Re-validates choice lock conditions on RPC receipt — never trusts client state.
  On Chooser disconnect: EndSession(EDialogueEndReason::ChooserDisconnected).

Client (Chooser):
  Receives FDialogueClientState via ClientRPC on UDialogueManagerComponent.
  Renders speaker, line text, and choices.
  Counts down timeout locally from TimeoutRemainingSeconds — no per-tick server sync.
  Sends ServerRPC_SubmitChoice(SessionID, ChoiceIndex, TargetComponent) for choices.
  Sends ServerRPC_AcknowledgeLine(SessionID, TargetComponent) for ACK-required lines.

Client (Observer, Group sessions only):
  Receives same FDialogueClientState (bIsObserver=true).
  Choice submissions are suppressed client-side and rejected server-side.
  Choices shown read-only if UDialogueNode_PlayerChoice::bShowChoicesToObservers.
```

---

## Known Issues

| # | Issue | Severity | Notes |
|---|---|---|---|
| KI-1 | `FDialogueSimulator::BuildContext` has no way to distinguish multiple session variables with the same `FGameplayTag` value — all tag variants are merged into `SourceTags` as a flat set. | Low | Session variables are short-lived; value collision is a designer error catchable in dev builds. |
| KI-2 | `Server_ReceiveACK` only validates that the current node is a `UDialogueNode_Line`, but does not validate that `Sender` is a participant in the session. A spoofed ACK from any actor with access to `UDialogueManagerComponent` would advance the session. | Medium | Should add a participant membership check matching the pattern in `Server_ReceiveChoice`. |
| KI-3 | `UDialogueManagerComponent::EndPlay` calls `Server_NotifyChooserDisconnect` for all sessions in `SessionOwners`, but does not distinguish whether the disconnecting player was the Chooser or an Observer for each session. Observer disconnects will trigger `EndSession` unnecessarily. | Medium | `Server_NotifyChooserDisconnect` should check `Session.GetChooser()` against the disconnecting player's pawn before calling `EndSession`. The current implementation in `UDialogueComponent` partially handles this but relies on the `Chooser` weak pointer having already gone null, which is a race condition. |
| KI-4 | The `bRequiresAck=false` (auto-advance) path in `RunSession` does not push `FDialogueClientState` to clients before advancing, meaning auto-advance lines are invisible to clients if no waiting step follows them. | Medium | `PushClientState` should be called before advancing for non-ACK lines. The current design only calls `PushClientState` on `WaitForACK` or `WaitForChoice`. |
| KI-5 | `UDialogueNode_Condition` calls `BuildContextFromSession(Session)` — this function is referenced in node code but not defined in the node or simulator. It should be `FDialogueSimulator::BuildContext(Session)`. | High | Orphaned reference — must be replaced with the correct call before implementation. |
| KI-6 | `FDialogueSessionEventMessage` (for `Session.Started`) carries `TObjectPtr<UDialogueAsset> Asset` but is broadcast via GMS, which may outlive the asset if the session ends before listeners process the event. Should be `TSoftObjectPtr`. | Low | No crash risk in practice since the asset is a `UDataAsset` pinned by the content browser, but the type is inconsistent with the rest of the system's lifetime safety approach. |

---

## File Structure

```
GameCore/Source/GameCore/
└── Dialogue/
    ├── DialogueEnums.h                    // EDialogueSessionMode, EDialogueEndReason,
    │                                      //   EDialogueStepAction, EDialogueVariantType
    ├── DialogueTypes.h                    // FDialogueVariant, FDialogueStackFrame,
    │                                      //   FDialogueSession, FDialogueClientChoice,
    │                                      //   FDialogueClientState, FDialogueEventMessage,
    │                                      //   FDialogueStepResult, FDialogueSessionEventMessage,
    │                                      //   FDialogueSessionEndedMessage
    ├── DialogueSimulator.h / .cpp         // FDialogueSimulator
    ├── Assets/
    │   └── DialogueAsset.h / .cpp         // UDialogueAsset
    ├── Nodes/
    │   ├── DialogueNode.h                 // UDialogueNode (abstract base)
    │   ├── DialogueNode_Line.h / .cpp
    │   ├── DialogueNode_PlayerChoice.h / .cpp
    │   ├── DialogueNode_Condition.h / .cpp
    │   ├── DialogueNode_Event.h / .cpp
    │   ├── DialogueNode_SetVariable.h / .cpp
    │   ├── DialogueNode_SubDialogue.h / .cpp
    │   ├── DialogueNode_Jump.h / .cpp
    │   └── DialogueNode_End.h / .cpp
    └── Components/
        ├── DialogueComponent.h / .cpp     // UDialogueComponent
        └── DialogueManagerComponent.h / .cpp  // UDialogueManagerComponent

GameCore/Source/GameCoreEditor/
└── Dialogue/
    ├── DialogueAssetTypeActions.h / .cpp  // FAssetTypeActions_DialogueAsset
    ├── SDialoguePreviewWidget.h / .cpp    // SDialoguePreviewWidget
    └── DialoguePreviewContext.h / .cpp    // FDialoguePreviewContext,
                                           //   UDialogueEditorChooserProxy
```

### Build.cs

```csharp
// GameCore.Build.cs — additions required:
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameplayTags",
    "GameplayMessageSubsystem",
});
// No dependency on Interaction, Quest, Progression, Serialization, or any other
// GameCore feature system. Only: RequirementSystem (for URequirement_Composite)
// and the backend (for FGameCoreBackend logging).

// GameCoreEditor.Build.cs:
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core", "CoreUObject", "Engine",
    "GameCore",
    "UnrealEd",
    "AssetTools",
    "SlateCore", "Slate",
    "GameplayTags",
});
```

### Gameplay Tag Config

Add to `Config/Tags/GameCore.Dialogue.ini`:

```ini
[/Script/GameplayTags.GameplayTagsList]
+GameplayTagList=(Tag="DialogueEvent",DevComment="Root tag for all dialogue system GMS events")
+GameplayTagList=(Tag="DialogueEvent.Session",DevComment="Session lifecycle events")
+GameplayTagList=(Tag="DialogueEvent.Session.Started",DevComment="Session started")
+GameplayTagList=(Tag="DialogueEvent.Session.Ended",DevComment="Session ended")
+GameplayTagList=(Tag="DialogueEvent.Node",DevComment="Node execution events")
+GameplayTagList=(Tag="DialogueEvent.Node.Event",DevComment="UDialogueNode_Event fired")
+GameplayTagList=(Tag="DialogueEvent.End",DevComment="End reason tags for UDialogueNode_End")
+GameplayTagList=(Tag="Dialogue.Speaker",DevComment="Root tag for speaker identity tags")
+GameplayTagList=(Tag="Log.Dialogue",DevComment="Dialogue system log routing tag for FGameCoreBackend")
```
