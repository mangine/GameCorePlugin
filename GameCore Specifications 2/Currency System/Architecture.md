# Currency System — Architecture

**Module:** `GameCore`  
**Location:** `GameCore/Source/GameCore/Currency/`  
**Status:** Active Specification | UE 5.7

The Currency System manages any form of numeric value that can be owned, gained, spent, or transferred between actors — gold, gems, reputation, tokens, escrow funds. It is entity-agnostic: any Actor can hold a wallet. All mutations are server-authoritative and fully audited.

---

## Dependencies

### Unreal Engine Modules
| Module | Reason |
|---|---|
| `GameplayTags` | Currency identifiers, audit event tags |
| `Net` | `FFastArraySerializer`, `COND_OwnerOnly` replication |

### GameCore Plugin Systems
| System | Role |
|---|---|
| **GameCore Backend** | All audit dispatch (`FGameCoreBackend::GetAudit`) and logging (`FGameCoreBackend::GetLogging`) |
| **Serialization System** | `UCurrencyWalletComponent` implements `IPersistableComponent` for persistent wallets |

> The Currency System has **no dependency** on the Event Bus, Requirement System, or State Machine. It is intentionally one of the lowest-level systems in the plugin.

---

## Requirements

- Multiple distinct currencies per wallet, each identified by `FGameplayTag`.
- Wallets attach to any Actor: `APlayerState`, guild actors, trade session actors, NPCs, banks.
- Each currency has independent `Min`/`Max` clamp. Negative balances supported when explicitly configured.
- All mutations are **server-authoritative** — clients never write wallet state directly.
- Every mutation carries both a `Source` and `Target` identity for full audit traceability.
- Atomic transfers: both sides mutate or neither does. No partial state.
- Trade/escrow wallets are **ephemeral** — not persisted; crash recovery relies on audit log replay.
- All mutation outcomes use `EWalletMutationResult` — silent no-ops are unacceptable.

---

## Design Decisions

| Decision | Rationale |
|---|---|
| `TMap<FGameplayTag, FCurrencySlotConfig>` in `UCurrencyWalletDefinition` | O(1) config lookup, no tag duplication, clean authoring surface |
| `int64` amounts | MMORPG economies overflow `int32`; gold sinks and inflation require the range |
| Component is a dumb state owner | All policy (validation, audit, clamping) lives in `UCurrencySubsystem` |
| FastArray replication (`COND_OwnerOnly`) | Delta-sends only changed slots; wallet balances are private per-player |
| `EWalletMutationResult` return codes | Callers handle failure explicitly — no silent no-ops for economy mutations |
| Both `Source` and `Target` on every mutation | Audit trail requires both sides of every economic event |
| Trade wallets not registered with `UPersistenceSubsystem` | Ephemeral by design; crash recovery via audit log SessionId-pair replay |
| Definition is a `UDataAsset` | Never replicated, never saved — read-only authoring data resolved at `BeginPlay` |
| Anti-cheat via offline audit analysis | No live rate limiting; anomaly detection runs against audit records |
| All backend calls via `FGameCoreBackend` statics | Canonical GameCore access pattern — no direct `UE_LOG`, no subsystem lookup |
| Wallet broadcasts `OnCurrencyChanged` delegate | Decoupled; UI and game logic bind to the component, not the subsystem |
| `UCurrencyWalletComponent` implements `IPersistableComponent` | Reuses the persistence system for save/load; ledger serializes by tag |

---

## System Units

| Unit | Class | Role |
|---|---|---|
| Wallet Definition | `UCurrencyWalletDefinition` | `UDataAsset` declaring currencies and their clamp configs |
| Wallet State | `UCurrencyWalletComponent` | `UActorComponent` owning the runtime ledger; replicates to owner |
| Ledger Entry | `FCurrencyLedgerEntry` | `FFastArraySerializerItem` — one entry per currency tag |
| Ledger Container | `FCurrencyLedger` | `FFastArraySerializer` — holds all entries, back-pointer to component |
| Slot Config | `FCurrencySlotConfig` | Per-currency clamp and initial amount, lives in the definition |
| Mutation Result | `EWalletMutationResult` | Enum returned by all mutation operations |
| Mutation Entry Point | `UCurrencySubsystem` | Server-only `UWorldSubsystem`; sole authority for all mutations |

---

## Logic Flow

### Single Wallet Mutation
```
Caller (server-side game code)
  └── UCurrencySubsystem::ModifyCurrency(Wallet, Tag, Delta, Source, Target, SessionId)
        ├── ValidateWallet(Wallet, Tag, OutConfig)
        │     ├── Null check + HasAuthority check → InvalidWallet
        │     ├── Null Definition check → InvalidWallet
        │     └── FindSlotConfig(Tag) → CurrencyNotConfigured if missing
        ├── GetOrCreateEntry(Tag) → FCurrencyLedgerEntry*
        ├── Clamp check: NewAmount = Current + Delta
        │     ├── < Config.Min → InsufficientFunds (negative delta) or ClampViolation
        │     └── > Config.Max → ClampViolation
        ├── Entry.Amount = NewAmount
        ├── Ledger.MarkItemDirty(*Entry) → FastArray replication queued
        ├── Wallet->OnCurrencyChanged.Broadcast(Tag, OldAmount, NewAmount)
        ├── NotifyDirty() → UPersistenceRegistrationComponent::MarkDirty (if registered)
        └── DispatchAudit(TAG_Audit_Currency_Modify, ...)
```

### Atomic Transfer
```
Caller (trade system / market)
  └── UCurrencySubsystem::TransferCurrency(From, To, Tag, Amount, Source, Target, SessionId)
        ├── Guard: Amount > 0 → ClampViolation if not
        ├── ValidateWallet(From, ...) — both must pass before any mutation
        ├── ValidateWallet(To, ...)
        ├── Pre-flight: From.Amount - Amount >= FromConfig.Min → InsufficientFunds
        ├── Pre-flight: To.Amount + Amount <= ToConfig.Max → RecipientClampViolation
        ├── Apply From: Amount -= Amount  +  MarkItemDirty + NotifyDirty
        ├── Apply To:   Amount += Amount  +  MarkItemDirty + NotifyDirty
        ├── From->OnCurrencyChanged.Broadcast(...)
        ├── To->OnCurrencyChanged.Broadcast(...)
        ├── DispatchAudit(TAG_Audit_Currency_Transfer,       From, ..., SessionId)  ← debit
        └── DispatchAudit(TAG_Audit_Currency_TransferCommit, To,   ..., SessionId)  ← credit
              Both entries share SessionId — used to pair them for crash recovery queries.
```

### Crash Recovery Flow (Conceptual — Game Layer)
```
On server session start:
  Query IAuditService for TAG_Audit_Currency_Transfer with no matching TAG_Audit_Currency_TransferCommit
  (matched by SessionId field)
  For each incomplete transfer:
    Locate escrow/trade wallet actor
    Call UCurrencySubsystem::ModifyCurrency to return funds to source
    Source = TAG_Source_Admin_Recovery
    DispatchAudit(TAG_Audit_Currency_Recovery, ...)
```

### Persistence Flow
```
Actor BeginPlay (server)
  └── UCurrencyWalletComponent::BeginPlay
        ├── HasAuthority check
        ├── Definition null check → log error + return
        ├── Initialize Ledger from Definition.Slots (skips already-loaded entries)
        └── Ledger.OwningComponent = this

PersistenceSubsystem::LoadPayload
  └── IPersistableComponent::Serialize_Load
        └── Restores FCurrencyLedgerEntry items by tag from FArchive

PersistenceSubsystem::SavePayload (triggered by dirty)
  └── IPersistableComponent::Serialize_Save
        └── Writes all FCurrencyLedgerEntry items to FArchive
```

### Client Replication Flow
```
Server: Ledger.MarkItemDirty
  └── FastArray delta packet queued for owning client
Client: FCurrencyLedgerEntry::PostReplicatedChange
  └── Wallet->OnCurrencyChanged.Broadcast(Tag, OldAmount, NewAmount)
        └── Bound UI / listeners update display
```

---

## Known Issues

1. **`GetOrCreateEntry` creates entries for unconfigured tags on transfer.** `ValidateWallet` checks the definition before `GetOrCreateEntry` is called, so this is guarded — but the order dependency is fragile. See Code Review.

2. **`COND_OwnerOnly` is hardcoded** in `UCurrencyWalletComponent::GetLifetimeReplicatedProps`. Guild, bank, and trade wallets need different replication conditions. This requires subclassing or a custom condition approach. See Code Review.

3. **Crash recovery is a conceptual note, not a spec.** The game layer is responsible for implementing the audit-replay recovery logic. The subsystem does not automate it.

4. **`ISourceIDInterface` GUIDs are zero-filled** inside the plugin. The game layer is responsible for resolving Actor → GUID before constructing the audit entry. This is by design but creates a gap in audit fidelity if the game layer neglects it.

5. **No event bus integration.** `OnCurrencyChanged` is a raw multicast delegate. Systems that want to listen globally (e.g. across actors, from a quest tracker) must have a direct reference to the component. See Code Review for the tradeoff analysis.

---

## File Structure

```
GameCore/
└── Source/
    └── GameCore/
        └── Currency/
            ├── CurrencyTypes.h               ← Enums, FCurrencySlotConfig, FCurrencyLedgerEntry, FCurrencyLedger
            ├── CurrencyWalletDefinition.h    ← UCurrencyWalletDefinition UDataAsset
            ├── CurrencyWalletDefinition.cpp
            ├── CurrencyWalletComponent.h     ← UCurrencyWalletComponent + IPersistableComponent impl
            ├── CurrencyWalletComponent.cpp
            ├── CurrencySubsystem.h           ← UCurrencySubsystem
            └── CurrencySubsystem.cpp
```
