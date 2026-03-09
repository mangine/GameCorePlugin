# Serialization System

# Serialization System

The Serialization System provides a **generic, actor-agnostic persistence layer** for the GameCore plugin. It handles snapshotting actor state into binary payloads, managing dirty tracking, schema versioning with automatic migration, and broadcasting completed payloads to external transport layers. It has **no knowledge of storage backends** — that concern belongs to game-specific modules.

---

## Backend Compatibility

The Serialization System is designed to integrate cleanly with `UGameCoreBackendSubsystem` but does **not depend on it**. Wiring is opt-in and lives in the game module.

| Concern | Interface | Notes |
| --- | --- | --- |
| Payload storage | `IDBService` | `FEntityPersistencePayload` maps directly to `IDBService::Set(PersistenceTag, EntityId, Data)`. See `IDBService` for the wiring example. |
| Capacity / error logging | `ILoggingService` | `UPersistenceSubsystem` calls `ILoggingService` via `UGameCoreBackendSubsystem`. Falls back to `UE_LOG` automatically if not connected. |

> See **GameCore Backend → IDBService** and **GameCore Backend → ILoggingService** for wiring examples.
> 

---

## Save Model

The system uses a single configurable cycle. Every `SaveInterval` seconds a save cycle fires. The cycle runs as a **partial save** (dirty components only) until `PartialSavesBetweenFullSave` partials have occurred, at which point the next cycle is a **full save** (all components). A single `SaveCounter` increments forever; full saves are determined by modulo — no explicit reset needed.

```jsx
PartialSavesBetweenFullSave = 3:

Cycle 1  → Partial  (dirty components only)
Cycle 2  → Partial
Cycle 3  → Partial
Cycle 4  → Full     ← SaveCounter % (3+1) == 0
Cycle 5  → Partial
...

PartialSavesBetweenFullSave = 0:

Every cycle → Full  ← 0 partials between fulls, always full
```

A server crash loses at most `SaveInterval` seconds of data. Setting `PartialSavesBetweenFullSave = 0` disables partial saves entirely. Both payload types are stamped with `EPayloadType` so the transport layer knows what it received.

---

## Scope & Responsibilities

- Serialize actor component state into `FEntityPersistencePayload` binary blobs
- Stamp every payload as `EPayloadType::Partial` or `EPayloadType::Full`
- Track dirty actors via a `DirtySet`, process within per-tick budget on partial cycles
- On full cycles, serialize **all** registered actors regardless of dirty state
- Preserve serialized data for actors that die before flush via a `SaveQueue`
- Automatically call `Migrate()` when a saved schema version mismatches the current version
- Broadcast payloads via **tag-keyed delegates** — transport is external and tag-routed
- On single-actor events (logout, zone transfer, trigger): force immediate full save
- On server shutdown: full save all registered entities synchronously, drain immediately
- Log capacity alerts via `ILoggingService` (falls back to `UE_LOG` if not wired)

---

## Key Design Decisions

| Decision | Choice | Rationale |
| --- | --- | --- |
| Serialization format | Binary (`FArchive` / `FMemoryWriter`) | 3–5× smaller and faster than JSON at MMORPG scale |
| Save cadence model | Configurable partial + full cycles | Generic — supports full-only (0 partials) or mixed cadence |
| Cycle counter | Single `SaveCounter` with modulo | No reset logic, clean and cheap |
| Actor discovery | Registration via `UPersistenceRegistrationComponent` | O(1) lookup, no world iteration cost |
| Dirty tracking | `TSet<FGuid>` DirtySet | Deduplication free, no pointer retention |
| Dead actor data | `TMap<FGuid, FEntityPersistencePayload>` SaveQueue | Data outlives actor, bounded by disconnect rate |
| Schema migration | Interface method `Migrate()` auto-called on version mismatch | Component author owns migration, subsystem orchestrates |
| Parallelism | Serialize on game thread, I/O dispatch on thread pool | UObject safety; I/O is the bottleneck |
| Transport routing | `FGameplayTag`-keyed delegate map | Extensible, no hardcoded enums, game defines own categories |
| Logging | `ILoggingService` via `UGameCoreBackendSubsystem` | Decoupled alerting; routes to `UE_LOG` when not connected |

---

## Component Overview

```jsx
┌─────────────────────────────────────────────────────────┐
│                    Actor (Any persistent actor)          │
│                                                         │
│  ┌──────────────────────────────────────────────────┐   │
│  │  UPersistenceRegistrationComponent               │   │
│  │  - PersistenceTag (FGameplayTag)                 │   │
│  │  - CachedPersistables[]                          │   │
│  │  - bDirty, SaveGeneration                        │   │
│  └──────────────────┬───────────────────────────────┘   │
│                     │ owns / queries                     │
│  ┌──────────────────▼───────────────────────────────┐   │
│  │  Components implementing IPersistableComponent   │   │
│  │  - Serialize_Save / Serialize_Load               │   │
│  │  - GetSchemaVersion / Migrate                    │   │
│  │  - GetPersistenceKey                             │   │
│  │  - MarkDirty() → RegComp.MarkDirty()            │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────────┬───────────────────────────────┘
                          │ Register / Enqueue
                          ▼
┌─────────────────────────────────────────────────────────┐
│               UPersistenceSubsystem                      │
│                                                         │
│  RegisteredEntities: TMap<FGuid, TWeakObjectPtr<UPRC>>  │
│  DirtySet:           TSet<FGuid>                        │
│  SaveQueue:          TMap<FGuid, FEntityPersistPayload> │
│  SaveCounter:        uint32  (modulo drives Full/Partial)│
│                                                         │
│  SaveTimer ──► FlushSaveCycle()                         │
│      └── SaveCounter % (PartialSavesBetweenFullSave+1)?  │
│          ├── == 0 → BuildPayload(Full), all actors      │
│          └── != 0 → BuildPayload(Partial), DirtySet     │
│                                                         │
│  DBFlushTimer ──► FlushSaveQueue() [drains SaveQueue]   │
│                                                         │
│  TagDelegates: TMap<FGameplayTag, FOnPayloadReady>      │
│       └──► routes payload to transport binding          │
└─────────────────────────────────────────────────────────┘
```

---

## Data Flows

### Periodic Save (Partial or Full)

```jsx
[SaveTimer fires → FlushSaveCycle()]
    │
    ├─ ++SaveCounter
    ├─ bFullSave = (SaveCounter % (PartialSavesBetweenFullSave + 1) == 0)
    │
    ├─ Partial: iterate DirtySet, N actors per tick budget
    └─ Full:    iterate ALL RegisteredEntities
    │
    ▼
[BuildPayload(bFullSave)] per actor → stamp EPayloadType
    │
    ▼
[Async(ThreadPool) → DispatchPayload → TagDelegate.Broadcast]
```

### Actor Death / Disconnect

```jsx
[Actor EndPlay triggered]
    │
    ├─ if GUID in DirtySet
    │       → BuildPayload(Full) immediately on game thread
    │       → MoveToSaveQueue(GUID, Payload)
    │
    └─ UnregisterEntity(GUID)

[Next DBFlushTimer] → FlushSaveQueue → DispatchPayload
```

### Event-Based Single Actor Save

```jsx
[RequestFullSave(Actor, Reason)]
    │
    ▼
[BuildPayload(Full) → EPayloadType::Full → MoveToSaveQueue]
    │
    ├─ Reason == Logout or ServerShutdown → FlushSaveQueue() immediately
    └─ else → wait for DBFlushTimer
```

### Server Shutdown

```jsx
[RequestShutdownSave()]
    │
    ├─ ClearTimer(SaveTimer)
    ▼
[Serialize ALL RegisteredEntities synchronously on game thread]
    │  EPayloadType::Full + ESerializationReason::ServerShutdown
    ▼
[DispatchPayload synchronously] → SaveQueue.Empty(), DirtySet.Empty()
```

### Load Path

```jsx
[RequestLoad(EntityId, Tag, OnComplete)]
    │
    ▼
[OnLoadRequested.Broadcast → transport fetches payload]
    │
    ▼
[OnRawPayloadReceived(Actor, Payload)]
    │
    ▼
[Per FComponentPersistenceBlob: find component by Key]
    ├─ Version mismatch → Migrate(Ar, From, To) → Serialize_Load
    └─ Version match    → Serialize_Load
```

---

## Event-Based Saves

| Event | API | Payload Type | Flush |
| --- | --- | --- | --- |
| Player logout | `EndPlay` → `MoveToSaveQueue` | Full | Immediate |
| Zone transfer | `RequestFullSave(ZoneTransfer)` | Full | Next DB flush |
| Custom trigger | `RequestFullSave(CriticalEvent)` | Full | Next DB flush |
| Server shutdown | `RequestShutdownSave()` | Full | Synchronous |

---

## Tag-Based Routing

```jsx
Persistence.Entity.Player  → PlayerDelegate  (e.g. DB service A)
Persistence.Entity.NPC     → NPCDelegate     (e.g. DB service B)
Persistence.Entity.Mob     → MobDelegate     (e.g. DB service B)
Persistence.World.State    → WorldDelegate   (e.g. DB service C)
Persistence.Custom.X       → game-defined    (registered at runtime)
```

Games register tags and bind delegates at startup. The subsystem routes with no knowledge of backends.

---

## Sub-Pages

[IPersistableComponent](Serialization%20System/IPersistableComponent%2031cd261a36cf8104ac4bfa71793916da.md)

[UPersistenceRegistrationComponent](Serialization%20System/UPersistenceRegistrationComponent%2031cd261a36cf8148b74edd1f5d5b54ff.md)

[UPersistenceSubsystem](Serialization%20System/UPersistenceSubsystem%2031cd261a36cf81f18420eec70b3e76e4.md)

---

## ⚠ Known Gaps & Future Work

- **Critical event trigger component** — a collision/overlap component to fire `RequestFullSave` on specific gameplay events is not yet specified
- **Per-tick budget on full saves** — `ActorsPerFlushTick` does not apply during full cycles; large servers may spike. Consider spreading full saves across multiple ticks
- **Load error path** — `OnComplete(false)` is never currently called; transport failure leaves `LoadCallbacks` entries dangling
- **Per-tag budget control** — all tags share a single `ActorsPerFlushTick`; high-priority tags (players) may be starved by lower-priority actors