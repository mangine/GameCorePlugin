# Zone System — Implementation Deviations

Deviations from the specification as documented in `GameCore Specifications 2/Zone System/`.

---

## DEV-1 — `AZoneActor::BroadcastStateChanged` takes a scope parameter

**Spec references:** `AZoneActor.md` shows `BroadcastStateChanged()` (no parameter) used from both `SetOwnerTag`/`AddDynamicTag`/`RemoveDynamicTag` (server, `ServerOnly`) and `OnRep_DynamicState` (client, `ClientOnly`).

**Implementation:** The private method signature is `BroadcastStateChanged(EGameCoreEventScope Scope)`. This avoids duplicating the message construction body and makes the intent explicit at each call site. Mutation methods pass `EGameCoreEventScope::ServerOnly`; `OnRep_DynamicState` passes `EGameCoreEventScope::ClientOnly`.

**Impact:** None. The spec's broadcast semantics are preserved exactly.

---

## DEV-2 — `Broadcast` uses raw `FInstancedStruct::Make` overload

**Spec references:** `AZoneActor.md` shows `Bus->Broadcast<FZoneStateChangedMessage>(...)` (typed template overload).

**Implementation:** Both `AZoneActor` and `UZoneTrackerComponent` use `Bus->Broadcast(Channel, FInstancedStruct::Make(Msg), Scope)` (raw overload). The typed template overload is marked internal-use-only in `GameCoreEventBus.h` and is reserved for intra-module use. Zone actors are in the same `GameCore` module so the template would be valid, but the raw form is used for consistency with the established pattern across all other systems in this session.

**Impact:** None. Both forms produce identical GMS broadcasts.

---

## DEV-3 — Channel tags declared in `namespace GameCore::Zone::Tags` (spec uses C++ namespace, not `UE_DECLARE_GAMEPLAY_TAG_EXTERN` at file scope)

**Spec references:** `ZoneMessages.md` already specifies the namespace form.

**Implementation:** Matches spec exactly. `ZoneChannelTags.h` wraps the `UE_DECLARE_GAMEPLAY_TAG_EXTERN` declarations inside `namespace GameCore::Zone::Tags`. `ZoneChannelTags.cpp` provides the `UE_DEFINE_GAMEPLAY_TAG` definitions inside the same namespace.

**Impact:** None.
