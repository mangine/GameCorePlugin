# Dialogue System — Code Review

---

## Overview

The Dialogue System is well-architected for a generic MMORPG plugin. The server-authority model is sound, the separation of interpreter (`FDialogueSimulator`) from ownership (`UDialogueComponent`) is clean, and the requirement/event system integration is properly decoupled. However, there are several bugs, design weaknesses, and missed opportunities that should be addressed before or during implementation.

---

## Critical Bugs

### B1 — Orphaned `BuildContextFromSession` Call
**File:** Original `UDialogueNode_Condition::Execute`  
**Severity:** High — compilation error  
`UDialogueNode_Condition::Execute` calls `BuildContextFromSession(Session)`, a function that does not exist. The correct call is `FDialogueSimulator::BuildContext(Session)`. This has been corrected in the new spec files, but it's worth flagging as an implementation-time check.

### B2 — `bRequiresAck=false` Lines Are Invisible to Clients (KI-4)
**File:** `UDialogueComponent::RunSession`  
**Severity:** Medium — silent content bug  
When `bRequiresAck=false`, `RunSession` returns `Continue` immediately without pushing `FDialogueClientState`. The client never sees the line — it's effectively invisible. `PushClientState` must be called even for auto-advance lines, followed immediately by advancing the node index. The corrected `RunSession` in this spec calls `PushClientState` on `WaitForACK` only, which means this is also not yet fixed in the new spec — the implementor must handle the `Continue` case from a Line node explicitly.

> **Fix:** In `FDialogueSimulator::Advance`, check if the node at the current index is a `UDialogueNode_Line` with `bRequiresAck=false` before returning `Continue`. Return `WaitForACK` instead (with a flag `bAutoAdvance=true`), so `RunSession` can push state and immediately advance. Alternatively, detect this in `RunSession` by reading the node after `Advance` returns `Continue`.

### B3 — ACK Can Be Sent by Non-Participants (KI-2)
**File:** `UDialogueComponent::Server_ReceiveACK`  
**Severity:** Medium — minor exploit surface  
The original spec did not validate that the ACK sender is a participant in the session. A client with access to any `UDialogueManagerComponent` could send an ACK for a session they are not part of. The corrected spec adds a participant membership check. Ensure the implementation includes this.

---

## Design Issues

### D1 — Observer Disconnect Ends Session (KI-3)
**File:** `UDialogueManagerComponent::EndPlay` + `UDialogueComponent::Server_NotifyChooserDisconnect`  
**Severity:** Medium — incorrect session termination  
`EndPlay` calls `Server_NotifyChooserDisconnect` for every session in `SessionOwners`, but Observers also have entries in `SessionOwners`. An Observer disconnecting will incorrectly end all sessions they were part of as if they were the Chooser. The fix is to check whether the disconnecting player's pawn matches `Session.GetChooser()` before calling `EndSession`. This requires `Server_NotifyChooserDisconnect` to accept an `AActor*` (the disconnecting pawn) and do the check internally, rather than relying on `UDialogueComponent::EndSession` to do it unconditionally.

### D2 — `FDialogueClientState::OwnerComponent` Carries a Hard Pointer via RPC
**File:** `FDialogueClientState`, `UDialogueComponent::PushClientState`  
**Severity:** Low — replication complexity  
Carrying `TObjectPtr<UDialogueComponent>` in an RPC struct requires the component to be replicated (which it is, for NetGUID). This is the correct approach, but it means the server RPC bandwidth includes the object reference on every state push. For groups, this is sent once per participant per state update — acceptable, but worth noting. An alternative would be a pre-registered session ID → component mapping on the client, but this would require a separate registration RPC which is worse. The current approach is correct.

### D3 — No Validation That `StartNodeIndex` Is Valid
**File:** `UDialogueAsset::IsDataValid` / `UDialogueComponent::StartGroupDialogue`  
**Severity:** Low — authoring error not caught early enough  
`IsDataValid` should check that `StartNodeIndex` is within bounds of `Nodes`. Currently, an out-of-bounds `StartNodeIndex` will cause an `AssetError` end at the first `ExecuteNode` call. This is not a crash, but it's a silent failure that could confuse designers. Add this check to `IsDataValid`.

### D4 — Session Variables Are a Flat TMap — No Type Safety
**File:** `FDialogueSession::Variables`, `UDialogueNode_SetVariable`  
**Severity:** Low — designer error surface  
`FDialogueVariant` is a manual discriminant union. There is no compile-time or editor-time check that prevents a designer from writing a `Bool` variable named `"MyVar"` in one node and reading it as an `Int` in a requirement. This is an inherent limitation of the approach, but the editor could warn on variable name reuse with mismatched types if the same `VariableName` appears in multiple `SetVariable` nodes with different `Type` values. Not blocking for initial implementation.

### D5 — `FDialogueSessionEventMessage::Asset` Was Originally a Hard Pointer
**Original spec:** `TObjectPtr<UDialogueAsset> Asset`  
**Fixed in new spec:** `TSoftObjectPtr<UDialogueAsset> Asset`  
GMS messages can outlive their producers. A hard `TObjectPtr` in a GMS message struct is technically safe as long as the message is processed synchronously, but it's inconsistent with the project's lifetime safety approach (`TWeakObjectPtr` for actors, soft refs for assets). The new spec uses `TSoftObjectPtr` — confirm this in implementation.

### D6 — `UDialogueNode_PlayerChoice::Execute` Does Not Evaluate Choice Locks for Client State
**File:** `UDialogueNode_PlayerChoice::Execute` → `BuildClientState`  
**Severity:** Design note  
Lock evaluation for `FDialogueClientChoice::bLocked` is done in `UDialogueComponent::BuildClientState`, not in `Execute`. This is the correct separation — `Execute` returns `WaitForChoice` and `BuildClientState` constructs the client-visible payload. However, this means `Execute` is essentially a no-op aside from returning the action. The current design is correct and intentional; just note it for future maintainers.

---

## Missing Features Worth Considering

### M1 — No Node-Level Condition on Line Nodes
There is no way to conditionally skip a line node without inserting a `Condition` node before it. At scale, this leads to verbose linear graphs: Line → Condition → Line or Line. A `UDialogueNode_Line::SkipCondition` (optional `URequirement_Composite`) would simplify many common patterns. Deferred — not blocking.

### M2 — No Random Line Node
MMORPGs commonly want NPCs to say one of N lines at random. There is no `UDialogueNode_RandomLine` or random-branch node. This is straightforward to add as a new node type and would be a high-value addition for NPC ambient dialogue. Deferred — not blocking for initial implementation.

### M3 — No Way to Query Active Sessions from Game Code
`UDialogueComponent::HasActiveSession(const AActor*)` is the only query method. Game code cannot enumerate all active sessions, check if a specific `UDialogueAsset` is running, or query a session's current state (which asset, which node). A `GetActiveSessions()` accessor or a richer query API would be useful. Add as a future improvement.

### M4 — No Replay / History for Clients
Clients only receive the *current* state. If a client UI is rebuilt (e.g. widget is destroyed and recreated during a session), there is no way to recover the previous line. A `GetActiveState(SessionID)` on `UDialogueManagerComponent` exists — ensure the UI uses it on widget construction to restore state after a rebuild.

### M5 — Auto-Advance Lines Need Explicit Handling
As noted in B2, `bRequiresAck=false` lines need special handling. The current design has a silent gap here. Consider adding a `bAutoAdvance` flag to `FDialogueStepResult` to communicate this intent explicitly from the simulator to `RunSession`, rather than relying on callers to check the node type after the fact.

---

## Positive Highlights

- **`FDialogueSimulator` as a plain struct** is the right call. No UObject lifecycle, fully testable in isolation, shared between server and editor without any coupling.
- **Server authority is strictly enforced.** No node indices, no asset references, no server-internal state ever travel to clients. The client state is a pure display snapshot.
- **Anti-cheat validation on every RPC receipt** (ResolveChoice re-evaluates locks) is correct and follows the established project pattern.
- **Chooser disconnect handling** is clean and unambiguous — no ambiguous state promotion.
- **GMS event decoupling** is properly done — the dialogue system broadcasts tagged messages and has zero knowledge of what listens.
- **Requirement system reuse** is textbook — no parallel condition framework, direct `URequirement_Composite` embedding in nodes.
- **Session variables as FRequirementContext injection** is a clever bridge that allows requirement-based branching on session-local state without extending the Requirement System.
- **`bInterruptOnNewSession`** being opt-in is the right default — silent no-ops are less surprising than silent interruptions for most dialogue scenarios.
