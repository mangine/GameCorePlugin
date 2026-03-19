# Dialogue System — Requirements and Design Decisions

**Sub-page of:** [Dialogue System](Dialogue%20System%20Overview.md)  
**Purpose:** Records original requirements, features added during design, key architectural decisions and rationale, and explicitly rejected alternatives. Authoritative record of *why* the system is designed this way.

---

## 1. Original Requirements

| # | Requirement |
|---|---|
| R1 | Dialogue flow must be authorable by designers without writing C++ or Blueprint logic. |
| R2 | Dialogue must support branching, converging paths, and reusable sub-dialogues (tunneling). |
| R3 | All dialogue conditions and choice locks must reuse the existing Requirement System — no parallel condition framework. |
| R4 | Dialogue must be able to trigger game-side systems (quests, cinematics, item grants) without the dialogue system importing those modules. |
| R5 | All line text must participate in the standard UE localization pipeline. |
| R6 | The server must be the sole authority — dialogue state must never be client-authoritative. |
| R7 | Single-player dialogue (one instigator) must be the default, low-overhead case. |
| R8 | Group dialogue (multiple participants, one Chooser) must be supported for group quest briefings and similar content. |
| R9 | An editor preview tool must allow designers to walk through a dialogue asset without launching the game. |
| R10 | The activation mechanism must be fully decoupled — game code wires `StartDialogue` to whatever event it needs. |

---

## 2. Features Added During Design

### Session Variables
Not in original requirements. Added to support simple per-session state (e.g. "has player seen this warning once") without requiring a game-side persistent store for trivially short-lived flags. Variables are `FName → FDialogueVariant` and exist only for the session lifetime.

### Timed Choices
Added to prevent one player from blocking a group session indefinitely. `UDialogueNode_PlayerChoice` gains `TimeoutSeconds` (0 = disabled) and `DefaultChoiceIndex`. The server auto-submits the default choice on timeout expiry via the same validation path as a player choice. `FDialogueClientState` carries `TimeoutRemainingSeconds` so the UI can render a countdown, driven locally after the initial state push with no per-tick replication.

### Observer Read-Only Choices
In group sessions, Observers receive the same `FDialogueClientState` as the Chooser. `UDialogueNode_PlayerChoice::bShowChoicesToObservers` controls whether choices are rendered as read-only for Observers. Useful for transparency in group content.

### Chooser Resolution Delegate
`UDialogueComponent::ResolveChooser` is a delegate defaulting to `Participants[0]`. Game code overrides it to implement group-leader-as-Chooser or similar policies without the dialogue system importing a group/party module.

### `bRequiresAck` on Line Nodes
Some lines should auto-advance (cutscene narration); others should wait for the player to press "continue". `UDialogueNode_Line::bRequiresAck` (default `true`) controls this. When `false`, the server advances immediately after pushing state to clients.

### Node-Level Speaker Tag
Each `UDialogueNode_Line` carries a `FGameplayTag SpeakerTag` rather than an actor reference or a name string. The client UI resolves display name, portrait, and audio from its own speaker registry keyed by tag. The dialogue system never touches actor references for speaker identity.

---

## 3. Architectural Decisions

### Decision: Custom Asset + Interpreter, not Inkpot
**Considered:** Inkpot (Ink narrative language integration for UE5).  
**Rejected because:**
- Ink compiles dialogue strings directly into JSON — no native `FText`/StringTable pipeline. At MMORPG scale this requires a bespoke extraction layer.
- Ink's runtime object is stateful and designed for single-client use. Server-authoritative MMORPG dialogue requires explicit ownership of session state, which fights the Ink model.
- `EXTERNAL` function bridging for Requirement System and GMS integration is verbose and untypesafe.
- Inkpot is a third-party plugin with limited UE5.7 compatibility guarantees.  
**Decision:** Custom `UDialogueAsset` + `FDialogueSimulator` interpreter. Full control over localization, replication, requirements integration, and editor tooling.

### Decision: Custom Asset, not State Machine repurposing
**Considered:** Driving dialogue as a `UStateMachineAsset` graph.  
**Rejected because:**
- State machines model persistent states with guarded transitions. Dialogue requires stacking (sub-dialogues), weaving (converging paths), per-line metadata (speaker, VO, timing), and inline conditions on individual lines — not just transitions.
- Every line would become a state node; large dialogues produce unreadable graphs.
- `UStateMachineComponent` lifecycle is designed for long-lived actor state, not a transient per-player conversation.  
**Decision:** Separate `UDialogueNode` hierarchy with a lightweight flat-array interpreter.

### Decision: Flat node array with integer indices, not a graph object model
Nodes in `UDialogueAsset` are stored as a `TArray<TObjectPtr<UDialogueNode>>`. Edges are `int32` indices into this array. No separate edge objects, no graph topology class.  
**Rationale:** Simpler serialization, trivial deep-copy for the simulator, and index stability after `BeginPlay` is easy to guarantee. Indices are internal — never sent over the wire.

### Decision: `FDialogueSimulator` is a plain struct, not a subsystem
The interpreter is `FDialogueSimulator` — a value type with a `Step()` method and a `FDialogueSession&` parameter. It has no UObject lifecycle, no tick, and no world reference.  
**Rationale:** The server's `UDialogueComponent` and the editor preview tool both need to run the interpreter. Extracting it as a plain struct eliminates duplication with zero coupling overhead.

### Decision: Chooser disconnect ends the session
**Considered:** Promoting the next participant to Chooser.  
**Rejected because:** Choice inheritance state is ambiguous (did the departed Chooser see the current line? which choices were valid for them?), and the complexity is not worth it for the rare disconnect case.  
**Decision:** `EndSession` is called with `EDialogueEndReason::ChooserDisconnected`. All participants receive `OnDialogueEnded`. Clean, unambiguous.

### Decision: Variable scope is session-only
Session variables (`FDialogueSession::Variables`) are discarded when the session ends. They are not persisted.  
**Rationale:** Cross-session memory ("has this player seen this lore before?") belongs in a game-side persistent store queried by a custom `URequirement` subclass. The dialogue system is not a persistence layer.

### Decision: No per-tick timeout replication
The server sends `TimeoutRemainingSeconds` once when the choice state is pushed. The client counts down locally. Drift of a second or two is acceptable for a UI countdown timer and avoids per-tick RPC overhead.  
**Note:** The server is always authoritative — if the client submits a choice before its local timer reaches zero but the server timer has already expired, the server has already auto-submitted the default. The client's late RPC is silently ignored.

---

## 4. Explicitly Rejected Features

| Feature | Reason |
|---|---|
| Mid-session persistence (save/resume) | Adds significant complexity; dialogue sessions are short-lived. Game code can re-trigger the asset from a checkpoint if needed. |
| Dialogue history / journal | Out of scope. Game code binds `OnDialogueEnded` / GMS events and maintains its own log. |
| Camera / cinematic control | `UDialogueNode_Event` fires a GMS event; the cinematic system handles it. Dialogue system imports nothing. |
| NPC animation control | Same pattern — GMS event, game-side listener. |
| Voice subtitle timing sync | Client drives timing locally after receiving `USoundBase`. No server round-trip. |
| Ink / Inkpot integration | See Decision above. |
| Multiple simultaneous sessions per instigator on the same component | One active session per instigator per `UDialogueComponent`. Attempting to start a second is a no-op by default (configurable to interrupt). |
