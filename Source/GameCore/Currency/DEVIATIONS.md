# Currency System — Implementation Deviations

## Overview
The Currency System implementation follows the spec closely. The deviations below are adaptations required to compile against the existing GameCore stub backend and the codebase's actual interface signatures.

---

## Deviation 1: `FGameCoreLoggingService` Single-Argument Call

**Spec:** `UCurrencySubsystem` and `UCurrencyWalletComponent` show logging calls with two arguments: `LogWarning(Category, Message)` and `LogError(Category, Message)`.

**Actual:** `FGameCoreLoggingService` (in `GameCoreBackend.h`) is a stub that only accepts a single `const FString& Message` argument. All logging calls have been adapted to embed the category into the message string:
- `LogWarning("ModifyCurrency failed validation ...")` (category embedded in message).
- `LogError("CurrencyWallet: <actor> has no Definition assigned.")` (prefix serves as category).

**Impact:** Logging output format differs from spec intent. The real backend implementation will have the two-argument overload; calls should be updated when the backend is replaced.

---

## Deviation 2: `DispatchAudit` Uses Stub No-Op

**Spec:** `DispatchAudit` constructs an `FAuditEntry` with `FAuditPayloadBuilder` and calls `FGameCoreBackend::GetAudit(EventTag)->RecordEvent(Entry)`.

**Actual:** `FAuditEntry` and `FAuditPayloadBuilder` are not defined in the current stub `GameCoreBackend.h`. The `RecordEvent` template on `FGameCoreAuditService` is a no-op variadic template. `DispatchAudit` calls `FGameCoreBackend::GetAudit(EventTag).RecordEvent()` with no arguments (compiles against the stub). All audit data (CurrencyTag, Delta, ResultingAmount, SessionId, Source, Target, wallet owner name) is documented inline in the function body for game-module wiring when the real backend is provided.

**Impact:** No audit records are written in the current implementation. Full audit fidelity requires replacing the stub backend. The function signature and call sites are correct and ready for the real implementation.

---

## Deviation 3: `TAG_Audit_*` and `TAG_Log_Economy` are Inline Tag Requests

**Spec:** References `TAG_Audit_Currency_Modify`, `TAG_Audit_Currency_Transfer`, `TAG_Audit_Currency_TransferCommit`, and `TAG_Log_Economy` as cached native tag handles in a `GameCoreCurrencyTags` namespace.

**Actual:** No `GameCoreCurrencyTags` namespace or `GameCoreEventTags` additions for Currency were created. Audit tags are requested inline via `FGameplayTag::RequestGameplayTag(TEXT("..."), false)` as `static` local variables in `CurrencySubsystem.cpp`. Logging calls use `FGameplayTag{}` (invalid tag — accepted by stub).

**Impact:** Minor: `RequestGameplayTag` with `false` (no assert on missing) returns an invalid tag if the tag is not in `DefaultGameplayTags.ini`. The stub ignores the tag argument anyway. When the game module adds the currency tags to `DefaultGameplayTags.ini` and installs a real backend, these will route correctly. A formal `GameCoreCurrencyTags` namespace with cached handles should be added at that time.

---

## Deviation 4: `IPersistableComponent::IsDirty()` Added

**Spec:** Does not explicitly list `IsDirty()` in the `UCurrencyWalletComponent` spec.

**Actual:** `PersistableComponent.h` declares `virtual bool IsDirty() const = 0;` as a pure virtual. `UCurrencyWalletComponent` implements it as `return bDirty;`.

**Impact:** None — satisfies the interface contract.

---

## Deviation 5: `FGuid SessionId = FGuid()` as UFUNCTION Default Parameter

**Spec:** Shows `FGuid SessionId = FGuid()` as a default parameter on `ModifyCurrency` and `TransferCurrency` UFUNCTIONs.

**Actual:** Implemented as-is. In UE5, struct default values in UFUNCTIONs are not forwarded to Blueprint as default pins for structs, but the C++ default value is valid. Blueprint callers must provide the SessionId explicitly; C++ callers can omit it.

**Impact:** Minor Blueprint usability limitation. Can be resolved with a Blueprint-callable wrapper that generates a new FGuid internally.

---

## Deviation 6: `BeginPlay` Sets `Ledger.OwningComponent` After Slot Initialization

**Spec:** `BeginPlay` code in the spec sets `Ledger.OwningComponent = this` at the end of `BeginPlay`, after the slot initialization loop.

**Actual:** Implemented identically — `Ledger.OwningComponent = this` is set after the initialization loop. This means `PostReplicatedAdd` callbacks from `MarkItemDirty` during initialization fire before `OwningComponent` is set, so `OnCurrencyChanged` is not broadcast during initial slot setup on the server. This is by design (spec comment: "Skips already-loaded entries").

**Impact:** None — consistent with spec design intent. The delegate fires only on actual mutations after initialization.

---

## Non-Deviations (Confirmed Spec-Compliant)

- `EWalletMutationResult` enum values match spec exactly.
- `FCurrencySlotConfig` uses `int64` with `TNumericLimits<int64>::Max()` default.
- `FCurrencyLedger` with `FFastArraySerializer` and `TStructOpsTypeTraits` implemented.
- `FastArray` callbacks (`PostReplicatedAdd`, `PostReplicatedChange`, `PreReplicatedRemove`) implemented in `CurrencyWalletComponent.cpp`.
- `ValidateWallet` checks null pointer, authority, Definition, and slot config in order.
- `TransferCurrency` validates both wallets before any mutation (atomic guarantee).
- Pre-flight clamp checks happen before any ledger state is written.
- `NotifyDirty` is a silent no-op when no `UPersistenceRegistrationComponent` is found (ephemeral/trade wallets).
- `Serialize_Save` / `Serialize_Load` use tag-string serialization per spec.
- `Serialize_Load` empties and rebuilds the ledger array.
- `COND_OwnerOnly` replication condition implemented as spec default.
- `ShouldCreateSubsystem` checks `NM_Client` to restrict to server only.
- No Event Bus dependency — `OnCurrencyChanged` multicast delegate only.
- `friend class UCurrencySubsystem` declared on `UCurrencyWalletComponent` for private method access.
