# Currency System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The Currency System manages any form of numeric currency — gold, gems, points, tokens, reputation, trade escrow — in a server-authoritative, audit-backed, and entity-agnostic way. It is the economic foundation of the game and is designed to attach to any Actor.

---

## Requirements

- Multiple distinct currencies must be supported, each identified by a `FGameplayTag`.
- Currencies may be owned by any entity: player characters, banks, guilds, trade sessions, NPCs.
- Each currency has independent clamp configuration (`Min`, `Max`). Negative values are supported.
- Currency can be gained or spent (positive and negative deltas).
- All mutations are **server-authoritative**. No client may directly mutate wallet state.
- Every mutation must carry a source and target identity for full audit traceability.
- Multi-wallet transfers must be **atomic** — partial state on crash is unacceptable.
- Trade wallets are **ephemeral** — they do not persist; audit logs are the crash recovery mechanism.

---

## Design Decisions

| Decision | Rationale |
|---|---|
| `TMap<FGameplayTag, FCurrencySlotConfig>` in the definition | Eliminates tag duplication, O(1) config lookup, clean authoring surface |
| No `FCurrencyConfig` intermediate struct | Direct definition lookup is sufficient; no runtime copy needed |
| `int64` for all amounts | MMORPG economies will overflow `int32` with inflation and gold sinks |
| Component is a dumb state owner | All policy (validation, audit, rate limiting) lives in `UCurrencySubsystem` |
| FastArray replication (`COND_OwnerOnly`) | Delta-sends only changed currency slots; player wallet never broadcasts to others |
| `EWalletMutationResult` return codes | Callers must handle failure explicitly — silent no-ops are unacceptable for economy |
| Both `Source` and `Target` on every mutation | Audit trail requires both sides of every economic event |
| Trade wallets not registered with `UPersistenceSubsystem` | Ephemeral by design; crash recovery via audit log replay, not snapshot restore |
| Definition is a `UDataAsset` | Never replicated, never saved — read-only authoring data resolved at `BeginPlay` |

---

## System Units

| Unit | Class | Role |
|---|---|---|
| Wallet Definition | `UCurrencyWalletDefinition` | DataAsset declaring which currencies a wallet holds and their clamp configs |
| Wallet State | `UCurrencyWalletComponent` | Actor component owning runtime ledger; replicates to owner |
| Ledger Entry | `FCurrencyLedgerEntry` | FastArray element holding amount per currency tag |
| Mutation Entry Point | `UCurrencySubsystem` | Server-only world subsystem; sole authority for all mutations |
| Mutation Result | `EWalletMutationResult` | Enum returned by all mutation operations |
| Slot Config | `FCurrencySlotConfig` | Per-currency clamp and initial amount config inside the definition |

---

## Where It Lives

```
GameCore/
└── Source/
    └── GameCore/
        └── Currency/
            ├── CurrencyTypes.h
            ├── CurrencyWalletDefinition.h / .cpp
            ├── CurrencyWalletComponent.h / .cpp
            └── CurrencySubsystem.h / .cpp
```

---

## Key Flows

### Single Wallet Mutation
```
[Game Code / Subsystem]
    UCurrencySubsystem::ModifyCurrency(Wallet, Tag, Delta, Source, Target, SessionId)
    │
    ├── Validate: wallet valid, currency configured, server authority
    ├── Resolve FCurrencySlotConfig from Wallet->Definition->Slots[Tag]
    ├── Clamp check: NewAmount = Clamp(Current + Delta, Config.Min, Config.Max)
    │   └── If clamped to same value → return EWalletMutationResult::ClampViolation
    ├── Apply: FCurrencyLedgerEntry.Amount = NewAmount
    ├── FastArray MarkItemDirty → replication fires
    ├── Dispatch OnCurrencyChanged delegate on component
    └── Audit: FGameCoreBackend::Audit(TAG_Audit_Currency_Modify, ...)
```

### Atomic Transfer
```
[Game Code / Trade System / Market]
    UCurrencySubsystem::TransferCurrency(From, To, Tag, Amount, Source, Target, SessionId)
    │
    ├── Validate both wallets, currency configured in both, Amount > 0
    ├── Check From has sufficient funds: From.Amount - Amount >= From.Config.Min
    ├── Check To can receive: To.Amount + Amount <= To.Config.Max
    ├── If either check fails → return appropriate EWalletMutationResult, no mutation
    ├── Apply From: Amount -= Delta
    ├── Apply To:   Amount += Delta
    ├── MarkItemDirty on both FastArrays
    ├── Dispatch OnCurrencyChanged on both components
    └── Audit: single transactional FAuditEntry group covering both sides
```

### Trade Crash Recovery (Conceptual)
```
On server session start:
    Query IAuditService for Audit.Currency.Transfer entries with no matching Audit.Currency.TransferCommit
    For each incomplete transfer:
        Determine escrow wallet (trade session actor)
        Return funds to source wallet via ModifyCurrency with Source = Source.Admin.Recovery
        Audit the recovery event
```

---

## Sub-pages

- [UCurrencyWalletDefinition](Currency%20System/UCurrencyWalletDefinition.md)
- [UCurrencyWalletComponent](Currency%20System/UCurrencyWalletComponent.md)
- [UCurrencySubsystem](Currency%20System/UCurrencySubsystem.md)
- [Currency Types](Currency%20System/CurrencyTypes.md)
