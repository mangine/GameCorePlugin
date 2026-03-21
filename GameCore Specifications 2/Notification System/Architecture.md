# Notification System — Architecture

A client-side aggregator that listens to GameCore Event Bus channels, groups incoming notifications by category tag, tracks viewed state in memory, and exposes delegates for UI binding. It has zero server interaction and zero persistence — it is a pure presentation-layer system.

---

## Dependencies

### Runtime Module Dependencies
| Module | Reason |
|---|---|
| `GameCore` | All runtime classes live here |
| `GameplayTags` | `FGameplayTag` for category and channel identification |
| `GameplayMessageRuntime` | `UGameplayMessageSubsystem`, `FGameplayMessageListenerHandle` — used internally by Event Bus |
| `DeveloperSettings` | `UDeveloperSettings` base for `UGameCoreNotificationSettings` |
| `Engine` | `FTimerHandle`, `ULocalPlayerSubsystem`, `UTexture2D` |

### GameCore Plugin System Dependencies
| System | Usage |
|---|---|
| **Event Bus** | `UGameCoreEventSubsystem` — channel listener registration via `StartListening` / `StopListening`. All channel subscriptions go through the Event Bus, never raw GMS directly. |

> No dependency on Requirement System, Serialization, Backend/Audit, Inventory, or any other GameCore feature system. The Notification System is a consumer-only leaf system.

---

## Requirements

| # | Requirement |
|---|-------------|
| R1 | The system MUST run only on local players (`ULocalPlayerSubsystem`). No server execution. |
| R2 | Event Bus channel subscriptions MUST be data-driven via `UNotificationChannelConfig`. No subclassing required to add new notification sources. |
| R3 | Every notification MUST carry a `FGameplayTag` category. Grouping is always tag-based. |
| R4 | Stacking rules (max count, stacked title format, auto-view-on-stack) MUST be declared per-category via `UNotificationCategoryConfig`. |
| R5 | Notifications MUST support optional expiry (`ExpirySeconds <= 0` means no expiry). The subsystem owns expiry timer management. |
| R6 | Viewed state is in-memory only. GameCore does NOT persist it. The game layer may read state and replay `MarkViewed` on load if needed. |
| R7 | The subsystem MUST expose `PushNotification` as a direct API for notifications that do not originate from an Event Bus event. |
| R8 | All configuration (channel config asset, category config asset) MUST be assigned via `UGameCoreNotificationSettings` (a `UDeveloperSettings` subclass), visible in Project Settings. |
| R9 | Channel-to-notification conversion MUST be done by a `UNotificationChannelBinding` adapter (Blueprint-subclassable). GameCore never knows about game-specific message structs. |
| R10 | Notification IDs are ephemeral `FGuid` values generated at push time. No stable/deterministic IDs required. |
| R11 | The system MUST fire delegates: `OnNotificationAdded`, `OnNotificationViewed`, `OnNotificationExpired`, `OnGroupChanged`, `OnAllViewed`. UI binds exclusively to these. |

---

## Features

- **Per-local-player subsystem** — each split-screen player has an independent notification state.
- **Tag-based grouping** — all entries sharing a `CategoryTag` live in one `FNotificationGroup`.
- **Per-category stacking rules** — max stack count, stacked title format, auto-view-on-stack, all configured via data assets.
- **Optional expiry** — game-thread timers auto-dismiss entries after a configurable duration.
- **Viewed state tracking** — individual, category-wide, and global mark-viewed APIs.
- **Dismiss APIs** — individual and category-wide dismiss, decoupled from viewed state.
- **Delegate-driven UI** — five delegates cover all state change cases; UI never polls.
- **Direct push API** — `PushNotification` bypasses Event Bus for tutorial popups, in-world triggers, etc.
- **Data-driven channel config** — new notification sources require only a new data asset entry, no code change.
- **Blueprint-subclassable bindings** — `UNotificationChannelBinding` can be subclassed in Blueprint for zero-C++ notification sources.
- **Metadata escape hatch** — `FNotificationEntry::Metadata` passes arbitrary key/value context to UI without versioning the struct.

---

## Design Decisions

**`ULocalPlayerSubsystem` instead of `UActorComponent`**
Notifications are per-local-player, not per-actor. A component requires choosing an owner actor and managing attachment lifetime unnecessarily.

**Config via `UDeveloperSettings`**
Gives a Project Settings UI panel with no runtime assignment. The subsystem calls `GetDefault<UGameCoreNotificationSettings>()` at `Initialize`. Assets stored as `TSoftObjectPtr` to avoid hard-loading at startup; resolved synchronously at `Initialize` which runs during world setup.

**`UNotificationChannelBinding` adapter per channel**
Keeps GameCore free of game-specific Event Bus message types. The binding's `RegisterListener` virtual is the only place a game struct is referenced. Blueprint-subclassable so no C++ is required for simple cases.

**`RegisterListener` virtual on `UNotificationChannelBinding`**
The binding itself owns listener registration rather than the subsystem iterating and calling a raw GMS API. This allows typed C++ bindings to register a properly-typed listener internally and capture the payload before calling `BuildEntry`. Blueprint bindings use the default implementation which fires an untyped listener and pulls state from already-replicated game objects inside `BuildEntry`.

**Suppression via invalid `CategoryTag`**
Bindings return an `FNotificationEntry` with `CategoryTag.IsValid() == false` to suppress a notification. This avoids a separate `bSuppress` flag and makes the contract simple: a valid entry has a valid category.

**`FGameplayTag` for categories**
Hierarchical, editor-friendly, no enum maintenance. Parent-tag queries work for free.

**Expiry managed in the subsystem, not the UI**
The UI should not be trusted to clean up logical state. The subsystem fires `OnNotificationExpired` and removes the entry; the UI reacts.

**No persistence in GameCore**
GameCore is a generic library. Viewed/dismissed state is game-specific. The game serializes the GUIDs it wants to remember and replays `MarkViewed` on session restore.

**`CachedTotalUnviewed` write-through counter**
Avoids iterating all groups on every `GetTotalUnviewedCount()` call. Every code path that changes `bViewed` or removes an unviewed entry must update both the group's `UnviewedCount` AND the cache. This is an invariant enforced by discipline, not the type system — see Known Issues.

---

## Logic Flow

```
[Event Bus channel fires]
    └─► UNotificationChannelBinding::RegisterListener (lambda, registered at subsystem Initialize)
            └─► captures typed payload → calls BuildEntry_Implementation
                    └─► returns FNotificationEntry (or invalid CategoryTag to suppress)
                            └─► UGameCoreNotificationSubsystem::HandleIncomingEntry
                                    └─► PushEntryInternal
                                            ├─► assign FGuid, FDateTime::UtcNow
                                            ├─► get FNotificationCategoryRule from CategoryConfig
                                            ├─► bAutoViewOnStack → MarkViewed loop on existing entries
                                            ├─► MaxStackCount eviction (FIFO, evict oldest)
                                            ├─► append entry, update UnviewedCount + CachedTotalUnviewed
                                            ├─► StartExpiryTimer (if ExpirySeconds > 0)
                                            └─► broadcast OnNotificationAdded, OnGroupChanged

[Expiry timer fires]
    └─► OnEntryExpired(FGuid)
            ├─► ExpiryTimers.Remove (handle already stale — remove map entry)
            ├─► OnNotificationExpired.Broadcast
            └─► DismissNotification (cleans up Groups, fires OnGroupChanged)

[UI calls MarkViewed(Id)]
    └─► FindEntry → set bViewed, update counters
            └─► OnNotificationViewed, OnGroupChanged, [OnAllViewed if total == 0]

[Game code calls PushNotification(Entry)]
    └─► PushEntryInternal (same path as GMS-sourced entry)
```

---

## Known Issues

### 1. `CachedTotalUnviewed` desync risk
`CachedTotalUnviewed` is a write-through integer. Any code path that changes viewed state or removes an unviewed entry without correctly decrementing both `Group.UnviewedCount` and `CachedTotalUnviewed` produces a silent badge count desync. There is no self-healing recalculation. See Code Review for a suggested fix.

### 2. `FindEntry` linear scan
`FindEntry` iterates all groups and all entries. For typical notification counts (< 100 entries total) this is fine. For systems that abuse the notification store as a persistent record, performance will degrade. The design intent is short-lived transient notifications — enforce this via `MaxStackCount` in config.

### 3. No cook-time validation that `Channel` tags in bindings are distinct
Two bindings with the same `Channel` tag will register two listeners for the same channel, producing duplicate notifications. `IsDataValid` on `UNotificationChannelConfig` should check for duplicate channel tags but this is not currently specified.

### 4. Blueprint bindings race on replicated state
Blueprint bindings that pull data from replicated components inside `BuildEntry` can silently produce empty or stale entries if the replication hasn't arrived yet when the Event Bus event fires. The suppression contract (`CategoryTag.IsValid() == false`) handles this, but designers must remember to implement the guard.

### 5. Seed reproducibility for viewed-state restore is fragile
The session-restore pattern (`MarkViewed` on load using saved GUIDs) only works if notifications are re-generated with the same GUIDs, which is impossible because GUIDs are generated fresh at push time. The pattern is therefore only useful for filtering already-open panels on initial frame — not true persistence. See Code Review.

---

## File Structure

```
GameCore/
  Source/
    GameCore/
      Public/
        Notification/
          FNotificationEntry.h
          FNotificationGroup.h
          FNotificationCategoryRule.h
          UNotificationCategoryConfig.h
          UNotificationChannelBinding.h
          UNotificationChannelConfig.h
          UGameCoreNotificationSettings.h
          UGameCoreNotificationSubsystem.h
      Private/
        Notification/
          UNotificationCategoryConfig.cpp
          UNotificationChannelBinding.cpp
          UGameCoreNotificationSubsystem.cpp
```

> All types except the subsystem are lightweight enough to live in headers. The subsystem implementation belongs in a dedicated `.cpp` due to its size.
