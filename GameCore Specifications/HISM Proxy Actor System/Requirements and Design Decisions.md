# Requirements and Design Decisions — HISM Proxy Actor System

**Part of: GameCore Plugin** | **Status: Active** | **UE Version: 5.7**

This document records the **original requirements**, **explicit feature decisions**, and **architectural rationale** for the HISM Proxy Actor System. It exists so that any developer can understand not just *what* the system does, but *why* it was built the way it was, and *what alternatives were considered and rejected*.

---

## Problem Statement

A pirate MMORPG world contains large numbers of world props — trees, rocks, barrels, fishing spots, ore nodes — that must be renderable at high density and also be individually interactable, harvestable, or otherwise gameplay-active when players are nearby.

`UHierarchicalInstancedStaticMeshComponent` (HISM) solves the rendering problem: it batches thousands of identical meshes into a handful of draw calls at near-zero CPU cost. But HISM instances are **not Actors**. They have no individual identity, cannot host components (`UInteractionComponent`, `UStaticMeshComponent` with different materials, GAS components), and are invisible to every gameplay system in GameCore.

The alternative — placing thousands of individual Actors — would be CPU-prohibitive on a dedicated server at MMO scale and would eliminate all batching benefits on clients.

---

## Core Requirements

These are the non-negotiable requirements established at system inception:

**R1 — HISM instance replacement.** Any HISM instance must be replaceable with a fully functional Blueprint Actor when a player is nearby, and restored to HISM rendering when no player is nearby.

**R2 — Seamless visual transition.** The replacement actor must be placed at the exact world transform of the HISM instance (position, rotation, scale). The HISM instance must be hidden simultaneously. No visual pop or overlap is acceptable.

**R3 — Zero runtime spawning on the hot path.** Proxy actors must be pre-allocated at world start. Activation must be a transform set + visibility toggle, never a `SpawnActor` call. Runtime spawning (pool growth) is a permitted safety net but must never be the primary path.

**R4 — Arbitrary proxy actor content.** The proxy actor base class must impose no restrictions on what components or gameplay systems the concrete Blueprint subclass can host. Any `AActor` composition must be valid.

**R5 — Server authority only.** Pool management, proximity checks, and slot lifecycle must run exclusively on the server. Proxy actors replicate to clients via UE's standard Actor relevancy. No custom per-client visibility logic.

**R6 — Player-proximity driven activation.** Proxies activate when any player on the server is within a configurable radius of the HISM instance. All players must be checked — not just a single reference player.

**R7 — Hysteresis.** Proxies must not deactivate immediately when a player leaves range. A configurable delay must keep the proxy live after the last player leaves range. If a new player enters range during the delay, the timer cancels and the proxy stays active.

**R8 — Multi-mesh support on a single host actor.** A single forest actor must support multiple tree species (Oak, Pine, Birch) without requiring multiple host actors or complex game-side wiring. Each mesh type manages its own HISM component and proxy pool independently.

**R9 — Size variation without extra components.** Different sizes of the same mesh type (small oak, large oak) must be representable as per-instance transform scale variation on a single HISM component. Separate HISM components per size variant are not required.

**R10 — Integration with GameCore Interaction System.** Proxy actors must be compatible with `UInteractionComponent` without any changes to the Interaction System's scanner, validation, or server paths. From the scanner's perspective, a proxy is indistinguishable from a hand-placed Actor.

**R11 — Game system decoupling.** The proxy system must have zero knowledge of game-specific systems (harvesting, fishing, resource respawn). Integration is via bindable delegates and virtual hooks on the proxy actor, not hard dependencies.

**R12 — Spatial query efficiency.** Proximity checks across potentially 500+ instances per HISM component and 64+ players must not linearly scan all instances on every tick. A spatial acceleration structure must be used.

**R13 — Pool exhaustion graceful handling.** If the pool is exhausted, the system must log a warning and skip proxy activation rather than crash or spawn unbounded actors.

**R14 — Static instances only.** HISM instances managed by this system are baked at level load and never move. Dynamic HISM (procedurally repositioned) is explicitly out of scope.

---

## Feature Requirements

These features were explicitly requested during design:

**F1 — Editor host actor with auto-wiring.** Designers must be able to configure the entire system by adding entries to an `InstanceTypes` array on a single level-placed actor. HISM components and bridge components must be created automatically — no manual component setup.

**F2 — Foliage Tool integration.** Mass instance placement must be possible using UE's Foliage Tool (density brushes, slope filters, random scale). A one-click conversion utility must import all foliage instances of matching mesh types into the host actor and remove them from the Foliage Actor to prevent double rendering.

**F3 — Manual instance placement.** Individual instances must be placeable via an "Add Instance at Pivot" button in the Details panel, without opening a separate placement mode. The designer moves the host actor's pivot to the desired world position and clicks the button.

**F4 — Validation tooling.** A "Validate Setup" button must run a comprehensive authoring check: null references, missing components, `NumCustomDataFloats` value, zero-instance entries, pool size sanity. Results output to the Message Log.

**F5 — Editor undo support.** All editor-time mutations (adding instances, converting foliage) must be wrapped in `FScopedTransaction` and call `Modify()` before mutations, making them fully undoable via Ctrl+Z.

**F6 — Eligibility filtering.** External game systems must be able to veto proxy activation for specific instances (e.g. a harvested tree on respawn cooldown must not receive a proxy). This must be done via a bindable delegate, not by subclassing the bridge.

**F7 — Dynamic pool sizing.** The pool must grow beyond its pre-allocated `MinPoolSize` up to a `MaxPoolSize` ceiling when exhausted at runtime. Growth must be logged as a warning. The pool never shrinks during a session — memory is reclaimed on server restart.

**F8 — Pool sizing guidance.** The spec must provide a formula for computing `MinPoolSize` from instance density, activation radius, and expected concurrent player count, so designers can size pools correctly without guessing.

**F9 — GC-safe actor pooling.** Pooled actors must be kept alive by the UE garbage collector through a `UPROPERTY` reference on the bridge component. Raw pointers for slot access are permitted as a performance convenience but must not be the sole GC reference.

**F10 — Per-tick allocation-free proximity check.** Scratch containers used in the proximity tick must be pre-allocated member arrays cleared at tick start, not local variables constructed and destroyed each tick.

---

## Architectural Decisions

### AD-1: One HISM Component Per Mesh Type (not one HISM per host actor)

**Decision:** Each `FHISMProxyInstanceType` entry owns exactly one `UHierarchicalInstancedStaticMeshComponent`. A host actor with three tree species has three HISM components.

**Rationale:** HISM only batches identical meshes into a single draw call. A single HISM component containing mixed meshes provides no batching benefit. The correct model is one HISM per unique mesh — which is also what UE's own Foliage System does internally (`AInstancedFoliageActor` maintains one HISM component per `UFoliageType`). Having 12 HISM components on one actor for 3 species × 4 sizes was explicitly considered and rejected in favour of 3 components (one per species) with size variation handled by per-instance transform scale.

**Alternative considered and rejected:** One HISM component containing all instance types with a float type discriminator in `PerInstanceCustomData`. Rejected because it eliminates batching, complicates the bridge component (it would need to know which proxy class to use per instance), and provides no measurable benefit over separate components.

---

### AD-2: Size Variation via Per-Instance Transform Scale

**Decision:** Different sizes of the same mesh share one HISM component. Size is encoded in the instance's transform scale (e.g. 0.6x, 0.8x, 1.0x, 1.3x).

**Rationale:** HISM fully supports non-uniform per-instance scaling. The proxy actor inherits the instance's world transform via `SetActorTransform`, which includes scale. No additional components, data assets, or type discrimination is needed. Size variation is free.

**Alternative considered and rejected:** Separate HISM components per size variant (e.g. `HISM_SmallOak`, `HISM_MediumOak`, `HISM_LargeOak`, `HISM_HugeOak`). Rejected because it multiplies component count by the number of size tiers, adding setup overhead without any gameplay benefit.

---

### AD-3: Uniform 2D Spatial Grid (not Octree)

**Decision:** `FHISMInstanceSpatialGrid` is a flat 2D uniform grid over the XY plane, built once at `BeginPlay`.

**Rationale:** For static HISM instances (requirement R14) with a fixed, known query radius, a uniform grid has O(N) build time, O(1) average query time, high cache friendliness, and minimal implementation complexity. An octree would be justified only if instance density varied by many orders of magnitude across the world. For typical open-world prop placement this does not occur.

**Alternative considered:** Octree. Rejected: O(N log N) build, pointer-chased queries, more implementation complexity, no meaningful advantage for this use case.

**Z-axis note:** The grid is 2D (XY only). HISM instances for world props have minimal Z variance relative to the activation radius, so XY proximity is a correct and sufficient filter. The bridge performs a full 3D distance check on the candidates returned by the grid query. Documented explicitly so developers do not misinterpret the grid as a 3D structure.

---

### AD-4: PerInstanceCustomData for HISM Instance Hiding

**Decision:** When a proxy is live for instance `i`, that instance is hidden by writing `1.0` to `PerInstanceCustomData[0]`. The HISM material reads slot 0 and clips the pixel. When the proxy deactivates, `0.0` is written and the instance renders normally.

**Rationale:** HISM does not support per-instance visibility toggling through any direct API. The only reliable GPU-side mechanism is custom data that the material can read and act on. Alternative approaches considered:

- **Removing and re-adding instances:** Correct but expensive. Re-adding an instance requires rebuilding the HISM cluster tree. Unacceptable on the hot path.
- **Moving instance below terrain:** Cheap but hacky. Creates invisible collision, breaks navmesh queries, and the instance still exists in the spatial hash.
- **`SetCustomDataValue` with a material clip:** Chosen. Write is O(1), renders correctly, is deterministic, and adds no collision or navmesh side effects.

**Slot assignment:** Slot 0 is reserved as the hide flag. Slot 1 is the type index (written by `AHISMProxyHostActor`). Game-specific custom data starts at slot 2. `NumCustomDataFloats` is set to 2 minimum by the host actor at component creation time.

---

### AD-5: Pool Spawned Near Host Actor, Far Below Terrain

**Decision:** Pool actors are spawned at `HostActorLocation + (0, 0, -100000)` — in the same streaming cell as the host but 1km below the terrain floor.

**Rationale:** Spawning at `FTransform::Identity` (world origin) was the initial design but was rejected because:
- It places actors at a potentially visible location (terrain surface at world origin).
- It affects navmesh generation if the navmesh covers that area.
- It places actors in a potentially different streaming cell from the host, which could affect level streaming.

Spawning near the host's XY position keeps actors in the same cell. The extreme negative Z ensures they are below all terrain, invisible, outside any navmesh, and cannot affect collision queries.

---

### AD-6: Dynamic Pool with No Shrink

**Decision:** Pool starts at `MinPoolSize`, grows by `GrowthBatchSize` steps up to `MaxPoolSize` when exhausted. Pool never shrinks during a session.

**Rationale:** The alternatives were:
- **Strict pre-allocation only:** Safe and simple, but risks wasted memory if `MinPoolSize` is over-estimated, or runtime failures if under-estimated with no recovery path.
- **Shrink on inactivity:** Adds significant complexity (timer management, safe destruction of slots with active timer handles), and the benefit is minimal because MMO dedicated servers restart periodically and memory is reclaimed at restart.

The hybrid approach (grow, never shrink) provides a safety net at minimal complexity cost. Every growth event is a `Warning` log so operations teams can detect and correct undersized `MinPoolSize` values in production.

**Pool exhaustion behavior:** If `MaxPoolSize` is reached and the pool is still exhausted, an `Error` is logged and activation is skipped for that instance. The instance remains a HISM render until a pool slot becomes available on the next proximity tick.

---

### AD-7: Tag-Based Component Ownership (not Name-Prefix Matching)

**Decision:** `CreateComponentsForEntry` writes `"HISMProxyManaged"` to `ComponentTags` on every HISM and Bridge component it creates. `DestroyOrphanedComponents` identifies managed components by checking `ComponentTags.Contains("HISMProxyManaged")`.

**Rationale:** An earlier design used name-prefix matching (`CompName.StartsWith("HISM_")`). This is fragile — a developer could add a component named `HISM_MyCollider` to the host actor for a legitimate purpose, and the orphan cleanup would destroy it incorrectly. Component tags are the correct UE mechanism for system-level ownership marking.

---

### AD-8: bIsRebuilding Declared Outside #if WITH_EDITOR

**Decision:** The re-entry guard `bIsRebuilding` is declared as a `UPROPERTY() bool` outside any `#if WITH_EDITOR` guard, even though it is only used by editor-only code.

**Rationale:** `UCLASS` member variables inside `#if WITH_EDITOR` blocks produce different class memory layouts between editor and non-editor builds. UHT's generated serialization and reflection code assumes a single consistent layout. Placing a data member inside `#if WITH_EDITOR` causes a layout mismatch that leads to undefined behaviour during serialization. The correct options are: (a) declare outside the guard (costs 1 byte in shipping builds), or (b) use `UPROPERTY()` with `WITH_EDITORONLY_DATA`. Option (a) was chosen for simplicity.

---

### AD-9: GetCustomDataValue Does Not Exist — Fast Path Uses TypeIndex Only

**Decision:** `RebuildTypeIndices` fast path exits on `TypeIndex == i` without reading back `PerInstanceCustomData` from the HISM component.

**Rationale:** `UInstancedStaticMeshComponent` has no `GetCustomDataValue` method. Per-instance custom data is a write-only channel from the CPU — data goes directly into the GPU render buffer with no readable CPU copy. An earlier design attempted to sample slot 1 of instance 0 as a staleness check. This was identified as a compile failure and removed. The fast path based on `TypeIndex` field alone is correct and sufficient because every structural change (add, reorder, remove entry) calls `RebuildTypeIndices` via `PostEditChangeProperty`, which runs the slow path and updates `TypeIndex` before saving. `PostLoad` also unconditionally runs the slow path, handling any edge case from stale serialized values.

---

### AD-10: Foliage Converter Uses Three-Pass Algorithm with Descending Removal

**Decision:** `ConvertFoliageToProxyHost` operates in three passes: (1) collect all transforms into a local array, (2) add to host actor, (3) remove from foliage highest-index-first.

**Rationale:** The naive approach — accumulate indices during iteration, then remove — causes index invalidation. `RemoveInstance` on a `UHierarchicalInstancedStaticMeshComponent` compacts the instance array immediately. Removing index 5 from a 10-instance component shifts instances 6–9 down by one. Any previously recorded index ≥ 5 now references the wrong instance. Descending-order removal avoids this: removing the highest index first never shifts any index below it, so all lower indices remain valid throughout the operation.

`FFoliageInstanceId` (a stable ID type that would avoid this) is an internal Foliage module type not exposed in UE 5.7 public headers and cannot be used from a plugin.

---

### AD-11: FoliageActor->PostEditChange() for Post-Removal Refresh

**Decision:** After removing instances from the Foliage Actor, `FoliageActor->PostEditChange()` is called rather than `FFoliageInfo::Refresh(...)`.

**Rationale:** `FFoliageInfo::Refresh` changed its signature between UE 5.x minor versions. Calling it with a fixed argument list would compile on some versions and fail on others. `PostEditChange()` on the `AActor` base triggers the standard UObject change notification pipeline which causes the foliage actor to rebuild all internal state through a well-defined, stable path.

---

### AD-12: Slate Button Bindings Use CreateLambda, Not _Raw with Trailing Arguments

**Decision:** All `SButton::OnClicked` bindings in `FHISMProxyHostActorDetails` use `FOnClicked::CreateLambda(...)`.

**Rationale:** `FOnClicked` is declared as `TDelegate<FReply()>` — a zero-argument delegate. The `_Raw` binding variant with trailing payload arguments is only valid for delegates that declare those parameters in their type signature. For a zero-argument delegate, any payload must be captured via a lambda closure. Using `OnClicked_Raw(this, &Method, ExtraArg)` on a zero-argument delegate fails to compile.

---

### AD-13: Member Scratch Buffers for Proximity Tick

**Decision:** `TickInstancePlayerCount`, `TickPlayerPositions`, `TickCandidates`, `TickSlotsToDeactivate`, and `TickSlotsToRevive` are declared as private member arrays on `UHISMProxyBridgeComponent`, cleared with `Reset()` at tick start rather than constructed as local variables.

**Rationale:** Constructing a `TMap<int32,int32>` or `TArray<T>` as a local variable involves heap allocation on every construction. At 0.5s tick intervals across dozens of bridge components on a server, this produces thousands of allocator calls per second. `TArray::Reset()` zeroes the element count while retaining allocated capacity, meaning no heap interaction after the first tick. This is the standard UE pattern for hot-path containers.

---

## Explicitly Out of Scope

These features were discussed and deliberately excluded:

**Dynamic HISM instances (R14):** Instances that reposition at runtime are not supported. The spatial grid is built once at `BeginPlay`. If instances move, the bridge has no mechanism to update the grid. Systems requiring dynamic HISM are out of scope — use normal Actors.

**Multiple HISM components per bridge:** One bridge manages exactly one HISM component. This is enforced by `AHISMProxyHostActor` which creates exactly one bridge per `FHISMProxyInstanceType` entry. Heterogeneous HISM (multiple mesh types in one component) is not supported.

**Pool shrinking:** Pool actors are never destroyed during a session. Memory is reclaimed on server restart. The complexity of safe mid-session pool shrinking (timer handle management, safe slot destruction) was evaluated and rejected as not justified by the benefit.

**HISM-side gameplay collision:** The HISM component itself has collision disabled. All gameplay collision (interaction traces, physics) belongs to the proxy actor. This is intentional — HISM collision at 500+ instances would be expensive and redundant.

**Client-side proximity logic:** Proximity checks, pool management, and slot activation run exclusively on the server. Clients receive proxy actor state via standard Actor replication. No client-side proximity or pool code exists.

**Viewport click-to-place FEdMode:** The "Add Instance at Pivot" workflow requires moving the host actor's pivot to the desired position manually. A proper viewport placement mode (click terrain to place an instance) was identified as high value but deferred as future work.
