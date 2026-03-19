# Notification System

**Part of: GameCore Plugin** | **Status: Draft Specification** | **UE Version: 5.7**

---

## Purpose

A client-side aggregator that listens to GMS channels, groups incoming notifications by category tag, tracks viewed state in memory, and exposes delegates for UI binding. It has zero server interaction and zero persistence — it is a pure presentation-layer system.

---

## Sub-Pages

- [Types & Data Assets](Types%20and%20Data%20Assets.md) — `FNotificationEntry`, `FNotificationGroup`, `UNotificationChannelBinding`, `UNotificationChannelConfig`, `UNotificationCategoryConfig`, `UGameCoreNotificationSettings`
- [Subsystem](Subsystem.md) — `UGameCoreNotificationSubsystem` full class definition, lifecycle, internal logic
- [GMS Integration](GMS%20Integration.md) — how channels map to notifications, binding adapter pattern, Blueprint binding guide
- [Usage Guide](Usage%20Guide.md) — how to wire the system from game code, sample C++ and Blueprint integration

---

## Requirements

| # | Requirement |
|---|-------------|
| R1 | The system MUST run only on local players (`ULocalPlayerSubsystem`). No server execution. |
| R2 | GMS channel subscriptions MUST be data-driven via `UNotificationChannelConfig`. No subclassing required to add new notification sources. |
| R3 | Every notification MUST carry a `FGameplayTag` category. Grouping is always tag-based. |
| R4 | Stacking rules (max count, label format, auto-view-on-stack) MUST be declared per-category via `UNotificationCategoryConfig`. |
| R5 | Notifications MUST support optional expiry (`ExpirySeconds <= 0` means no expiry). The subsystem owns expiry timer management. |
| R6 | Viewed state is in-memory only. GameCore does NOT persist it. The game layer may read state and replay `MarkViewed` on load if needed. |
| R7 | The subsystem MUST expose `PushNotification` as a direct API for notifications that do not originate from a GMS event. |
| R8 | All configuration (channel config asset, category config asset) MUST be assigned via `UGameCoreNotificationSettings` (a `UDeveloperSettings` subclass), visible in Project Settings. No runtime assignment required. |
| R9 | Channel-to-notification conversion MUST be done by a `UNotificationChannelBinding` adapter (Blueprint-subclassable). GameCore never knows about game-specific GMS message structs. |
| R10 | Notification IDs are ephemeral `FGuid` values generated at push time. No stable/deterministic IDs required. |
| R11 | The system MUST fire delegates: `OnNotificationAdded`, `OnNotificationViewed`, `OnNotificationExpired`, `OnGroupChanged`, `OnAllViewed`. UI binds exclusively to these. |

---

## Key Decisions

| Decision | Rationale |
|----------|-----------|
| `ULocalPlayerSubsystem` instead of `UActorComponent` | Notifications are per-local-player, not per-actor. A component would require choosing an owner actor and managing attachment lifetime unnecessarily. |
| Config via `UDeveloperSettings` (`UGameCoreNotificationSettings`) | Gives a Project Settings UI panel with no runtime assignment needed. The subsystem calls `GetDefault<UGameCoreNotificationSettings>()` at `Initialize`. |
| `UNotificationChannelBinding` adapter per channel | Keeps GameCore free of game-specific GMS message types. The binding is the only place a game struct is referenced. Blueprint-subclassable so no C++ required for simple cases. |
| `FGameplayTag` for categories | Hierarchical, editor-friendly, no enum maintenance. Parent-tag queries work for free (e.g. `Notification.Category` matches all children). |
| Expiry managed in the subsystem, not the UI | The UI should not be trusted to clean up logical state. The subsystem fires `OnNotificationExpired` and removes the entry; the UI reacts to the delegate. |
| No persistence in GameCore | GameCore is a generic library. Viewed/dismissed state is game-specific. The game serializes the GUIDs it wants to remember and replays `MarkViewed` on session restore. |
| `PushNotification` direct API | Escape hatch for game-layer notifications with no GMS origin (tutorials, in-world triggers, etc.). Produces the same delegate chain as a GMS-sourced push. |

---

## Module Dependencies

```csharp
// GameCore.Build.cs — Notification module
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameCore",
    "GameplayTags",
    "GameplayMessageRuntime",
    "DeveloperSettings",   // for UDeveloperSettings
    "Engine"
});
```

No dependency on any other GameCore feature system. Depends only on Event Bus (via `UGameCoreEventSubsystem` for GMS access) — and even that is optional if `PushNotification` is the only entry point used.

---

## Layer Position

Sits at **Layer 5 (Client Presentation)** — it never emits GMS events itself, never mutates game state, and has no authority-side logic. It is purely a consumer.
