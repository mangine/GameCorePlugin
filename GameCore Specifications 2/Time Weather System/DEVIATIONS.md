# Time Weather System — Implementation Deviations

Deviations from the specification as documented in `GameCore Specifications 2/Time Weather System/`.

---

## DEV-1 — `UGameCoreEventSubsystem` renamed to `UGameCoreEventBus`

**Spec references:** All spec files use `UGameCoreEventSubsystem::Get(this)` and `UGameCoreEventSubsystem` as the type name.

**Implementation:** The event bus is `UGameCoreEventBus` (established in the GameCore Backend implementation). All spec call sites — `RegisterActiveEvent`, `UnregisterActiveEvent`, `Broadcast`, `StartListening` — are unchanged; only the class name differs.

**Impact:** None at runtime. Any project code calling `UGameCoreEventSubsystem::Get` will not compile; use `UGameCoreEventBus::Get` instead.

---

## DEV-2 — Active Event Registry added to `UGameCoreEventBus` (not a new subsystem)

**Spec references:** `ActiveEventRegistry.md` presents the registry as an API extension to `UGameCoreEventSubsystem`.

**Implementation:** `FActiveEventRecord`, `RegisterActiveEvent`, `UnregisterActiveEvent`, `IsEventActive`, `GetActiveEvents`, `GetAllActiveEvents`, and `SweepExpiredEvents` are implemented directly on `UGameCoreEventBus` (in `GameCoreEventBus.h/.cpp`). No separate class was created.

**Impact:** None. The spec's intent (single shared registry per world) is preserved.

---

## DEV-3 — `PushSnapshotToClients` is a no-op stub

**Spec references:** `UTimeWeatherSubsystem.md` shows `PushSnapshotToClients` casting to `AMyGameState` (a game-project-specific class) and setting `GS->TimeSnapshot`.

**Implementation:** `PushSnapshotToClients()` is a no-op stub in the plugin. Hard-coupling a plugin to a game-project class (`AMyGameState`) would prevent the plugin from compiling in any project without that exact class.

**Recommended pattern for game projects:**
```cpp
// In your game module's UTimeWeatherSubsystem subclass or GameMode BeginPlay:
if (AMyGameState* GS = GetWorld()->GetGameState<AMyGameState>())
{
    GS->TimeSnapshot = TimeWeatherSubsystem->GetTimeSnapshot();
    GS->ForceNetUpdate();
}
```
Alternatively, expose a `TDelegate<void(const FGameTimeSnapshot&)> OnSnapshotReady` on the subsystem that game code can bind to.

---

## DEV-4 — `Broadcast` overload uses `FInstancedStruct::Make` wrapper

**Spec references:** Broadcast calls in the spec are written as:
```cpp
Bus->Broadcast(TAG_..., Payload, EGameCoreEventScope::ServerOnly);
```

**Implementation:** `UGameCoreEventBus::Broadcast` has two overloads: a typed template and a raw `FInstancedStruct` overload. The subsystem uses the raw overload explicitly (`FInstancedStruct::Make(Payload)`) to match the existing bus API contract. The typed template would also work but is marked internal-use-only in `GameCoreEventBus.h`.

**Impact:** None. Both forms produce identical GMS broadcasts.

---

## DEV-5 — `FScheduledEventTrigger` is a plain `struct` (not `USTRUCT`)

**Spec references:** `TimeWeatherTypes.md` defines `FScheduledEventTrigger` as a plain struct without `GENERATED_BODY()`.

**Implementation:** Kept as a plain (non-USTRUCT) struct. It is internal to `UTimeWeatherSubsystem::FContextState` and never serialised, replicated, or exposed to Blueprint. Adding `GENERATED_BODY()` would require moving it to its own header and adding module linkage — unnecessary overhead for an internal detail.

**Impact:** None.
