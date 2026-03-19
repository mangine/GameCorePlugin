# Scope Guard

**Sub-page of:** [Event Bus System 2](../Event%20Bus%20System%202.md)

The scope guard is identical to the one in GMS1 (`UGameCoreEventSubsystem`). It is documented here in full for GMS2 implementors who may not have read the GMS1 spec.

---

## EGameCoreEventScope

`EGameCoreEventScope` is defined in `GameCoreEventSubsystem.h` (GMS1 header) and shared by both buses. **Do not redefine it for GMS2** — include the GMS1 header.

```cpp
/**
 * Controls which machine(s) a broadcast is permitted to fire on.
 * The broadcaster decides scope — the bus enforces it.
 * This is NOT a replication mechanism; it is a guard against firing on the wrong machine.
 */
UENUM(BlueprintType)
enum class EGameCoreEventScope : uint8
{
    /** Broadcast only when running with authority (server / standalone).
     *  Use for all events that originate from server-side logic. Default. */
    ServerOnly,

    /** Broadcast only on the owning client.
     *  Requires data to already exist on the client via replication before broadcast fires. */
    ClientOnly,

    /** Broadcast on all machines.
     *  Use only when both sides independently need to react to the same logical event
     *  and the broadcaster fires on both machines (e.g. a component with AuthorityMode=Both). */
    Both,
};
```

---

## Evaluation Logic

```
ServerOnly → NM_Client?  → DROP
ClientOnly → not NM_Client? → DROP
Both       → always PASS
```

Standalone (`NM_Standalone`) is treated as server — `ServerOnly` passes on standalone.

---

## Rules

- **Default is `ServerOnly`**. Omitting the scope argument on `Broadcast` is intentionally conservative.
- **`ClientOnly` requires prior replication.** A client-side broadcast is a notification layer only — the data it references must already be present on the client via `FFastArraySerializer` or a replicated property before the broadcast fires.
- **`Both` is rare.** Only use when a component explicitly runs meaningful logic on both machines and both sides legitimately need to react independently.
- **Scope is not a replication primitive.** The bus never sends data across the network. Scope only guards against broadcasting on the wrong machine.
