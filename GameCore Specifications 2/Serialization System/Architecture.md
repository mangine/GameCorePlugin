# Serialization System — Architecture

## Overview

The Serialization System provides a **generic, server-side persistence layer** for the GameCore plugin. It snapshots actor component state into binary payloads, manages dirty tracking, drives configurable partial/full save cycles, handles schema versioning with automatic migration, and dispatches completed payloads to external transport layers via tag-keyed delegates.

It has **no knowledge of storage backends** — that responsibility belongs to `IKeyStorageService` and the game module that wires them together.

---

## Responsibility Boundary

| Layer | Owner | Responsibilities |
|---|---|---|
| **Serialization** | `UPersistenceSubsystem` | Which actors to serialize, when, dirty tracking, partial/full cycles, payload production, delegate dispatch |
| **Storage** | `IKeyStorageService` | Write-behind queue, flush timing, batching, priority lanes, retry, deduplication, reconnect |

`UPersistenceSubsystem` dispatches payloads immediately via tag-keyed delegates. It owns no save queue, no DB flush timer, and no priority queue.

---

## Dependencies

### Unreal Engine Modules
- `Engine` — `UGameInstanceSubsystem`, `UActorComponent`, `AActor`
- `GameplayTags` — `FGameplayTag` used for persistence tag routing
- `CoreUObject` — `UInterface`, `UFUNCTION`, `UPROPERTY`

### GameCore Plugin Systems
- **GameCore Backend** (`ILoggingService` via `FGameCoreBackend::GetLogging()`) — optional; falls back to `FNullLoggingService` → `UE_LOG` if backend is not live
- **ISourceIDInterface** — actors must implement this to provide a stable `FGuid`. Lives in `GameCore Core`.

### External (Game Module)
- `IKeyStorageService` — game module binds `FOnPayloadReady` delegates to storage. The serialization system has no compile-time dependency on it.

---

## Requirements

- **Server-only**: subsystem must not create on clients
- **Actor-agnostic**: any actor can participate by adding `UPersistenceRegistrationComponent`
- **Binary format**: `FArchive`/`FMemoryWriter` — 3–5× smaller and faster than JSON at MMORPG scale
- **Configurable cadence**: partial (dirty-only) and full (all components) cycles; fully tunable via `DefaultGame.ini`
- **No per-tick spike**: spread serialization across frames using an `ActorsPerFlushTick` budget
- **Schema migration**: automatic — component author implements `Migrate()`; subsystem calls it on version mismatch
- **Bounded registry**: NPCs/Mobs are respawnable world state — must not register by default. This is a precondition for safe synchronous shutdown saves.
- **Critical path safety**: logout, zone transfer, and server shutdown payloads always fire, even under load
- **No dangling callbacks**: all load callbacks eventually fire, either on data arrival, explicit failure, or timeout
- **Logging**: route capacity alerts and errors via `ILoggingService`; transparent fallback to `UE_LOG`

---

## Features

- Serialize actor component state into `FEntityPersistencePayload` binary blobs
- Stamp every payload as `EPayloadType::Partial` or `EPayloadType::Full`
- Track dirty actors via a `TSet<FGuid>` DirtySet; deduplicated, no pointer retention
- On partial cycles: process only dirty actors within per-tick budget
- On full cycles: snapshot registry at cycle start, spread across ticks using same budget
- Schema versioning + `Migrate()` auto-called on version mismatch
- Tag-keyed delegate dispatch — game registers tags and binds listeners
- Force immediate full save on single-actor events (logout, zone transfer, trigger) with appropriate `bCritical`/`bFlushImmediately` flags
- Synchronous shutdown save for all registered entities
- Load request API with timeout sweep — no dangling callbacks

---

## Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Serialization format | Binary (`FArchive`/`FMemoryWriter`) | 3–5× smaller and faster than JSON at MMORPG scale |
| Save cadence | Configurable partial + full cycles | Generic; supports full-only (`PartialSavesBetweenFullSave=0`) or mixed |
| Cycle counter | Single `uint32 SaveCounter` with modulo | No reset logic, cheap, wraps at ~4B cycles (~40k years at 300s/cycle) |
| Actor discovery | Registration via `UPersistenceRegistrationComponent` | O(1) lookup, no world iteration |
| Dirty tracking | `TSet<FGuid> DirtySet` | Deduplication free, no UObject pointer retention |
| Dirty clearing | Generation counter on `IPersistableComponent` | Prevents clearing dirty state that was re-set during an in-progress flush |
| Schema migration | `Migrate()` auto-called on version mismatch | Component author owns migration; subsystem orchestrates |
| Transport routing | `FGameplayTag`-keyed delegate map | Extensible; no hardcoded enums; game defines categories |
| Queue ownership | `IKeyStorageService` owns write-behind | Clean layer separation — subsystem serializes; DB service queues and delivers |
| Priority/flush flags | `bCritical` + `bFlushImmediately` on payload | DB service owns policy; subsystem sets intent based on `ESerializationReason` |
| Logging | `FGameCoreBackend::GetLogging()` | Decoupled; transparent fallback to `UE_LOG` when backend is absent |
| NPC/Mob persistence | Not registered by default | Keeps registry bounded → synchronous shutdown save is safe |
| Dirty fields in interface | Stored in the **implementing component** class, not the UInterface | UInterface cannot hold instance state; fields live where UObject memory management applies |

---

## Save Cycle Model

A single `SaveCounter` increments every `SaveInterval` seconds. Full vs partial is determined by modulo:

```
++SaveCounter;
bool bFullSave = (SaveCounter % (PartialSavesBetweenFullSave + 1) == 0);
```

```
PartialSavesBetweenFullSave = 3:
  Cycle 1 → Partial
  Cycle 2 → Partial
  Cycle 3 → Partial
  Cycle 4 → Full   ← SaveCounter % 4 == 0
  Cycle 5 → Partial ...

PartialSavesBetweenFullSave = 0:
  Every cycle → Full
```

Full cycles snapshot `RegisteredEntities` at start and spread across frames via `TickFullCycle()` timer. `ActorsPerFlushTick` applies to **both** partial and full cycles.

---

## Payload Flags

Every `FEntityPersistencePayload` carries two flags set by `UPersistenceSubsystem` based on `ESerializationReason`:

| Flag | Set when | Effect at DB layer |
|---|---|---|
| `bCritical` | Logout, ZoneTransfer, ServerShutdown | Priority lane — never dropped on overflow |
| `bFlushImmediately` | Logout, ServerShutdown | Bypasses write-behind queue — dispatched synchronously |

---

## Logic Flow

```
┌─────────────────────────────────────────────────────────┐
│                 Actor (Any persistent actor)            │
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
│  │  - bDirty, DirtyGeneration (on the component)   │   │
│  │  - NotifyDirty() → RegComp.MarkDirty()          │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────┬───────────────────────────────────┘
                      │ Register / EnqueueDirty
                      ▼
┌─────────────────────────────────────────────────────────┐
│               UPersistenceSubsystem                     │
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
│  Game Module Transport (binds FOnPayloadReady)          │
│                                                         │
│  IKeyStorageService::Set(Tag, EntityId, Data,           │
│      bFlushImmediately, bCritical)                      │
└─────────────────────────────────────────────────────────┘
```

### Dirty Flow
1. Component state changes → component calls `NotifyDirty(this)`
2. `NotifyDirty` lazy-resolves `UPersistenceRegistrationComponent` via `FindComponentByClass` (once per component lifetime), sets `bDirty = true` on itself, calls `RegComp->MarkDirty()`
3. `MarkDirty()` sets `bDirty` on the registration component (guard: no-op if already dirty) and calls `Subsystem->EnqueueDirty(GUID)`
4. On next partial cycle: subsystem iterates `DirtySet` up to `ActorsPerFlushTick`, calls `RegComp->BuildPayload(false)` per actor
5. `BuildPayload` increments `SaveGeneration`, serializes only dirty components, calls `ClearIfSaved(SaveGeneration)` per component
6. Subsystem calls `DispatchPayload` → fires the tag-keyed `FOnPayloadReady` delegate immediately

### Load Flow
1. Game module calls `Subsystem->RequestLoad(EntityId, Tag, OnComplete)`
2. Subsystem stores callback in `LoadCallbacks`, broadcasts `OnLoadRequested`
3. Game module transport fetches from DB, calls `Subsystem->OnRawPayloadReceived(Actor, Payload)`
4. Subsystem deserializes blobs into components, calls `Migrate()` on version mismatch, fires callback
5. `TickLoadTimeouts()` fires every 5s; callbacks older than `LoadTimeoutSeconds` fire `OnComplete(false)` and are removed

---

## Tag Routing

```
Persistence.Entity.Player  → PlayerDelegate  (e.g. IKeyStorageService "PlayerDB")
Persistence.Entity.NPC     → NPCDelegate     (e.g. IKeyStorageService "WorldDB")
Persistence.World.State    → WorldDelegate   (e.g. IKeyStorageService "WorldDB")
Persistence.Custom.X       → game-defined    (registered at runtime)
```

Games register tags and bind delegates at startup. The subsystem routes with no knowledge of backends.

---

## Known Issues

- **Load path has no retry**: `OnComplete(false)` is fired and the game module must handle retry itself. A built-in retry with backoff would be safer.
- **`FindComponentByClass` in `OnRawPayloadReceived`**: blob-to-component matching iterates `CachedPersistables` linearly by key. Fine for small component counts; could use a pre-built `TMap<FName, IPersistableComponent*>` cache inside `UPersistenceRegistrationComponent` for actors with many persistable components.
- **Partial cycle does not guarantee all dirty actors are flushed per cycle**: if `DirtySet.Num() > ActorsPerFlushTick`, leftover dirty actors are not processed until the next partial cycle. This is by design (no frame spikes), but means maximum dirty-actor lag is up to `2 × SaveInterval` in the worst case.
- **No PIE teardown guard**: `EndPlay` fires for all `EEndPlayReason` values; in PIE this triggers spurious saves. Must filter `EEndPlayReason::EndPlayInEditor`.
- **`ServerInstanceId` has no enforcement**: if unset, payloads are stamped with an invalid GUID; error is logged but the subsystem continues. Consider a `checkf` in non-shipping builds.

---

## File Structure

```
GameCore/
└── Source/
    └── GameCore/
        └── Persistence/
            ├── PersistenceTypes.h                        # Enums, FComponentPersistenceBlob, FEntityPersistencePayload
            ├── PersistableComponent.h/.cpp               # IPersistableComponent interface
            ├── PersistenceRegistrationComponent.h/.cpp   # UPersistenceRegistrationComponent
            └── PersistenceSubsystem.h/.cpp               # UPersistenceSubsystem
```
