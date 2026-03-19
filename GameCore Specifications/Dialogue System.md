# Dialogue System

**Module:** `GameCore` (plugin)  
**Status:** Specification — Pending Implementation  
**UE Version:** 5.7  
**Depends On:** GameCore (Requirement System, Event Bus, Backend, Gameplay Tags)

See full specification in the [Dialogue System](Dialogue%20System/) sub-folder.

| File | Contents |
|---|---|
| [Dialogue System Overview](Dialogue%20System/Dialogue%20System%20Overview.md) | Design principles, authority model, session lifecycle, quick usage guide |
| [Requirements and Design Decisions](Dialogue%20System/Requirements%20and%20Design%20Decisions.md) | Original requirements, decisions, rejected alternatives |
| [Data Assets and Node Types](Dialogue%20System/Data%20Assets%20and%20Node%20Types.md) | `UDialogueAsset`, all node subclasses |
| [Runtime Structs](Dialogue%20System/Runtime%20Structs.md) | `FDialogueSession`, `FDialogueClientState`, supporting types |
| [FDialogueSimulator](Dialogue%20System/FDialogueSimulator.md) | Core interpreter shared by server runtime and editor tool |
| [UDialogueComponent](Dialogue%20System/UDialogueComponent.md) | Server-authoritative NPC component |
| [UDialogueManagerComponent](Dialogue%20System/UDialogueManagerComponent.md) | Per-player component on `APlayerState` |
| [GMS Events](Dialogue%20System/GMS%20Events.md) | All GameplayMessage events emitted by the system |
| [Editor Preview Tool](Dialogue%20System/Editor%20Preview%20Tool.md) | `SDialoguePreviewWidget`, asset editor registration |
| [Integration Guide](Dialogue%20System/Integration%20Guide.md) | Setup checklist, wiring patterns, UI binding samples |
