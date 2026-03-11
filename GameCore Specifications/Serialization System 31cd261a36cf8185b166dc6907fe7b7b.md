# Serialization System

The Serialization System provides a **generic, actor-agnostic persistence layer** for the GameCore plugin. It handles snapshotting actor state into binary payloads, managing dirty tracking, schema versioning with automatic migration, and broadcasting completed payloads to external transport layers. It has **no knowledge of storage backends** — that concern belongs to `IKeyStorageService` and the game module that wires them together.

---

## Responsibility Boundary

The system is split cleanly into two layers:

| Layer | Owner | Responsibilities |
|---|---|---|
| **Serialization** | `UPersistenceSubsystem` | Which actors to serialize, when, dirty tracking, partial/full cycles, payload production, delegate dispatch |
| **Storage** | `IKeyStorageService` | Write-behind queue, flush timing, batching, priority lanes, retry, deduplication, reconnect |

`UPersistenceSubsystem` dispatches payloads immediately via tag-keyed delegates. It has no save queue, no DB flush timer, and no priority queue. `IKeyStorageService` absorbs each payload into its own write-behind queue and handles everything downstream.

---

## Backend Compatibility

The Serialization System is designed to integrate cleanly with `UGameCoreBackendSubsystem` but does **not depend on it**. Wiring is opt-in and lives in the game module.

| Concern | Interface | Notes |
|---|---|---|
| Payload storage | `IKeyStorageService` | `FEntityPersistencePayload` maps to `IKeyStorageService::Set(StorageTag, EntityId, Data, bFlushImmediately, bCritical)`. See `IKeyStorageService` for the wiring example. |
| Capacity / error logging | `ILoggingService` | `UPersistenceSubsystem` calls `ILoggingService` via `UGameCoreBackendSubsystem`. Falls back to `UE_LOG` automatically if not connected. |

---

## Save Model

The system uses a single configurable cycle. Every `SaveInterval` seconds a save cycle fires. The cycle runs as a **partial save** (dirty components only) until `PartialSavesBetweenFullSave` partials have occurred, at which point the next cycle is a **full save** (all components). A single `SaveCounter` increments forever; full saves are determined by modulo — no explicit reset needed.

```
PartialSavesBetweenFullSave = 3:

Cycle 1  → Partial  (dirty components only)
Cycle 2  → Partial
Cycle 3  → Partial
Cycle 4  → Full     ← SaveCounter % (3+1) == 0
Cycle 5  → Partial
...

PartialSavesBetweenFullSave = 0:

Every cycle → Full
```

A server crash loses at most `SaveInterval` seconds of data. Setting `PartialSavesBetweenFullSave = 0` disables partial saves entirely. Both payload types are stamped with `EPayloadType` so the transport layer knows what it received.

---

## Payload Flags

Every `FEntityPersistencePayload` carries two flags set by `UPersistenceSubsystem` based on `ESerializationReason`. These are forwarded by the game module to `IKeyStorageService::Set` as call parameters:

| Flag | Set when | Effect at DB layer |
|---|---|---|
| `bCritical` | Logout, ZoneTransfer, ServerShutdown | Placed in DB service priority lane — never dropped on overflow, flushed before normal entries |
| `bFlushImmediately` | Logout, ServerShutdown | Bypasses DB write-behind queue — dispatched synchronously to backend |

---

## Scope & Responsibilities

- Serialize actor component state into `FEntityPersistencePayload` binary blobs
- Stamp every payload as `EPayloadType::Partial` or `EPayloadType::Full`
- Track dirty actors via a `DirtySet`, process within per-tick budget on partial cycles
- On full cycles, snapshot all registered actors and serialize across ticks using the same per-tick budget
- Automatically call `Migrate()` when a saved schema version mismatches the current version
- Broadcast payloads via **tag-keyed delegates** — transport is external and tag-routed
- On single-actor events (logout, zone transfer, trigger): force immediate full save with appropriate flags
- On server shutdown: full save all registered entities synchronously with `bCritical=true`, `bFlushImmediately=true`
- Log capacity alerts via `ILoggingService` (falls back to `UE_LOG` if not wired)

---

## Key Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Serialization format | Binary (`FArchive` / `FMemoryWriter`) | 3–5× smaller and faster than JSON at MMORPG scale |
| Save cadence model | Configurable partial + full cycles | Generic — supports full-only (0 partials) or mixed cadence |
| Cycle counter | Single `SaveCounter` with modulo | No reset logic, clean and cheap |
| Actor discovery | Registration via `UPersistenceRegistrationComponent` | O(1) lookup, no world iteration cost |
| Dirty tracking | `TSet<FGuid>` DirtySet | Deduplication free, no pointer retention |
| Schema migration | Interface method `Migrate()` auto-called on version mismatch | Component author owns migration, subsystem orchestrates |
| Transport routing | `FGameplayTag`-keyed delegate map | Extensible, no hardcoded enums, game defines own categories |
| Queue ownership | `IKeyStorageService` owns write-behind queue | Clean layer separation — subsystem serializes, DB service queues and delivers |
| Priority / flush control | `bCritical` + `bFlushImmediately` flags on payload | DB service owns policy; subsystem sets intent based on reason |
| Logging | `ILoggingService` via `UGameCoreBackendSubsystem` | Decoupled alerting; routes to `UE_LOG` when not connected |

---

## Component Overview

```
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
│  │  - NotifyDirty() → RegComp.MarkDirty()          │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────┬───────────────────────────────────┘
                      │ Register / EnqueueDirty
                      ▼
┌─────────────────────────────────────────────────────────┐
│               UPersistenceSubsystem                      │
│                                                         │
│  RegisteredEntities: TMap<FGuid, TWeakObjectPtr<UPRC>>  │
│  DirtySet:           TSet<FGuid>                        │
│  SaveCounter:        uint32 (modulo drives Full/Partial) │
│                                                         │
│  SaveTimer ──► FlushSaveCycle()                         │
│      ├── Partial: serialize DirtySet within tick budget │
│      └── Full: snapshot all, spread across ticks        │
│                                                         │
│  DispatchPayload() ──► FOnPayloadReady delegate         │
│      (immediate, no intermediate queue)                 │
└─────────────────────┬───────────────────────────────────┘
                      │ FEntityPersistencePayload
                      │ + bCritical + bFlushImmediately
                      ▼
┌─────────────────────────────────────────────────────────┐
│  Game Module Transport (binds FOnPayloadReady)           │
│                                                         │
│  IKeyStorageService::Set(Tag, EntityId, Data,           │
│      bFlushImmediately, bCritical)                      │
│                                                         │
│  ┌───────────────────────────────────────────────────┐  │
│  │  IKeyStorageService write-behind queue             │  │
│  │  - Priority lane (bCritical) — never dropped      │  │
│  │  - Normal lane — bounded, oldest dropped          │  │
│  │  - Deduplication: newer write replaces pending    │  │
│  │  - Flush timer, pressure flush, batching, retry   │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

---

## Tag Routing

```
Persistence.Entity.Player  → PlayerDelegate  (e.g. IKeyStorageService "PlayerDB")
Persistence.Entity.NPC     → NPCDelegate     (e.g. IKeyStorageService "WorldDB")
Persistence.Entity.Mob     → MobDelegate     (e.g. IKeyStorageService "WorldDB")
Persistence.World.State    → WorldDelegate   (e.g. IKeyStorageService "WorldDB")
Persistence.Custom.X       → game-defined    (registered at runtime)
```

Games register tags and bind delegates at startup. The subsystem routes with no knowledge of backends.

---

## Sub-Pages

- IPersistableComponent
- UPersistenceRegistrationComponent
- UPersistenceSubsystem

---

## Known Gaps & Future Work

- **Critical event trigger component** — a collision/overlap component to fire `RequestFullSave` on specific gameplay events is not yet specified
- **Per-tag budget control** — all tags share a single `ActorsPerFlushTick`; high-priority tags (players) may be starved by lower-priority actors
