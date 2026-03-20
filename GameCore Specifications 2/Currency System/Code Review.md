# Currency System — Code Review

---

## Overview

The Currency System is well-scoped and architecturally clean. The separation between dumb state (`UCurrencyWalletComponent`) and policy authority (`UCurrencySubsystem`) is sound. FastArray replication and `int64` amounts are correct choices. The audit trail design is solid for offline anomaly detection.

The issues below are ordered from most impactful to minor.

---

## Issues

### 1. `GetOrCreateEntry` Creates Entries for Both Wallets Before Validation Completes in `TransferCurrency`

**Severity: Medium**

In `TransferCurrency`, `GetOrCreateEntry` is called on both wallets after validation passes. This is safe because `ValidateWallet` confirms the tag exists in the definition before entry creation. However, the order dependency is fragile: if the logic is ever reordered or `GetOrCreateEntry` is called before `ValidateWallet`, it creates ledger entries for unconfigured tags, which then silently carry zero amounts that bypass config. A better approach is to make `GetOrCreateEntry` assert (in non-shipping) that the tag is already confirmed valid, or rename it to signal its precondition.

**Suggestion:** Add a `checkf` in `GetOrCreateEntry` in non-shipping builds:
```cpp
checkf(Definition && Definition->FindSlotConfig(CurrencyTag),
    TEXT("GetOrCreateEntry called for unconfigured tag %s"), *CurrencyTag.ToString());
```

---

### 2. `COND_OwnerOnly` Hardcoded — Guild, Bank, Trade Wallets Cannot Override

**Severity: Medium**

`GetLifetimeReplicatedProps` on `UCurrencyWalletComponent` hardcodes `COND_OwnerOnly`. This is correct for player wallets but incorrect for guild/bank wallets (multiple authorized viewers) and trade wallets (exactly two participants). The spec documents this in a note, but does not provide a resolution path.

**Options:**
- Introduce a `EWalletReplicationCondition` property (`OwnerOnly`, `Custom`, `None`) and switch in `GetLifetimeReplicatedProps`.
- Alternatively, subclass `UCurrencyWalletComponent` per use case (`UGuildWalletComponent`, `UTradeWalletComponent`) and override `GetLifetimeReplicatedProps` there.

Subclassing is the cleaner path — it keeps the base component free of conditional branching and allows different actors to configure their own replication logic without polluting the base class.

---

### 3. `OnCurrencyChanged` OldAmount Is Always 0 on Client

**Severity: Low-Medium**

The FastArray `PostReplicatedChange` callback does not have access to the previous value. The spec documents this with a note, but it creates a subtle API hazard: listeners that bind `OnCurrencyChanged` on the client and assume `OldAmount` is meaningful will silently get wrong data. This has caused bugs in practice in similar economy systems.

**Suggestion:** Rename the delegate parameters `PreviousAmount`/`CurrentAmount` and add a comment flagging `PreviousAmount` as always 0 on the client. Consider storing a `PreviousAmount` field on `FCurrencyLedgerEntry` (not replicated, server-only shadow) or documenting clearly that delta computation on the client must use `New - Cached` from the UI layer.

---

### 4. No Event Bus Integration — Global Listeners Require Direct Component References

**Severity: Low-Medium**

The raw `FMulticastDelegate OnCurrencyChanged` on the component requires listeners to hold a direct pointer to the specific wallet component. This is fine for UI bound to the local player's wallet, but it makes global server-side listeners (e.g. a quest tracker monitoring gold changes across all players) awkward — they must hook every `APlayerState::BeginPlay` to bind delegates per wallet.

**Suggestion:** After mutation, broadcast a `FCurrencyChangedMessage` through `UGameCoreEventBus` (scope `ServerOnly`). Retain the component delegate for local/client binding. This follows the pattern already established by the Progression and State Machine systems and allows server-side systems to listen without per-actor binding.

```cpp
// In CurrencyTypes.h (new)
USTRUCT(BlueprintType)
struct GAMECORE_API FCurrencyChangedMessage
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<UCurrencyWalletComponent> Wallet;
    UPROPERTY() TWeakObjectPtr<AActor> OwnerActor;
    UPROPERTY() FGameplayTag CurrencyTag;
    UPROPERTY() int64 OldAmount = 0;
    UPROPERTY() int64 NewAmount = 0;
    UPROPERTY() int64 Delta     = 0;
};
```

---

### 5. `ISourceIDInterface` Leaves Actor GUIDs as Zero — Audit Entries Are Partially Incomplete

**Severity: Low**

The plugin fills `ActorDisplayName` and `SubjectTag` from `ISourceIDInterface` but leaves `ActorId`/`SubjectId` as zero GUIDs. The spec notes this is intentional (game layer resolves Actor → GUID). However, if the game layer neglects to do this, audit entries are partially incomplete and cannot be correlated with player accounts. This is a silent failure.

**Suggestion:** Document a concrete game-layer wrapper pattern — e.g. a `UCurrencyServiceFacade` in the game module that resolves GUIDs and delegates to the subsystem. This makes the gap explicit rather than hoping the game module fills it.

---

### 6. `BeginPlay` Initialization Does Not Handle Ledger Reordering After Load

**Severity: Low**

After persistence load, `Serialize_Load` restores entries in the order they were saved. `BeginPlay` then adds any missing slots. The combination is correct, but a future schema change that removes a currency from the definition while old save data still contains it will leave orphan `FCurrencyLedgerEntry` items in the ledger that no definition slot backs. These orphan entries are never mutated (since `ValidateWallet` would return `CurrencyNotConfigured`), but they consume replication bandwidth and may confuse tooling.

**Suggestion:** In `BeginPlay`, after initialization, prune ledger entries for tags no longer present in the definition, logging a warning per removed entry. Do this only if the definition is non-null.

---

### 7. `FAuditPayloadBuilder` Is Implicitly Assumed to Exist

**Severity: Low**

The `DispatchAudit` implementation uses `FAuditPayloadBuilder` as if it exists in the GameCore Backend, but no specification of this class was found. If it does not exist, this code will not compile. The audit entry `Payload` field may just be a `FString` — in that case `DispatchAudit` should use `FString::Printf` or a `TMap<FString, FString>` serialized as JSON.

**Suggestion:** Confirm `FAuditPayloadBuilder` exists in the Backend spec or replace with a direct `FString` payload.

---

## Positive Notes

- `int64` for amounts: correct for MMORPG economy scale.
- Validation-before-mutation pattern in `TransferCurrency` is the right approach — atomicity is guaranteed without a rollback mechanism.
- Audit `SessionId` pairing for crash recovery is a simple and effective pattern.
- `IPersistableComponent` reuse avoids reinventing persistence — correct application of the system.
- `NotifyDirty` silently no-ops on trade wallets (no `UPersistenceRegistrationComponent`) — clean ephemeral behavior without special casing.
- `SupportsCurrency` allows callers to probe before committing to a mutation path — good for UI code.
