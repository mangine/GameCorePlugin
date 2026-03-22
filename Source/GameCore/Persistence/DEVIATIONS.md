# Serialization System — Implementation Deviations

## DEV-01: `IsDirty()` virtual accessor added to `IPersistableComponent`

**Spec:** `BuildPayload` checks `Persistable->bDirty` directly.
**Actual:** A pure virtual `virtual bool IsDirty() const = 0;` was added to `IPersistableComponent`.
**Reason:** `bDirty` is an instance member on the implementing class (not on the interface, per spec design
rules). Accessing it directly from `BuildPayload` would require a cast to the concrete type, which breaks
the generic interface contract. Adding `IsDirty()` is the cleanest solution consistent with the spec's own
Code Review note ("The cleaner solution is adding `virtual bool IsDirty() const = 0;` to
`IPersistableComponent`"). Implementing classes return `bDirty` from this accessor.

---

## DEV-02: `ISourceIDInterface::GetEntityGUID()` added to existing interface

**Spec:** `UPersistenceRegistrationComponent::GetEntityGUID()` calls
`ISourceIDInterface::Execute_GetEntityGUID(GetOwner())`.
**Actual:** `GetEntityGUID()` (`BlueprintNativeEvent`, returns `FGuid`) was added to
`Core/SourceID/SourceIDInterface.h`. The default implementation returns `FGuid()`.
**Reason:** The existing `ISourceIDInterface` only declared `GetSourceTag()` and `GetSourceDisplayName()`.
`GetEntityGUID()` is required by the persistence system and was absent. Rather than creating a second
interface, it was added here where actors implementing `ISourceIDInterface` can override it. The default
returns an invalid GUID, which the subsystem logs as a warning and skips gracefully.

---

## DEV-03: `FGameCoreBackend::GetLogging()` signature adaptation

**Spec:** Logging calls written as `FGameCoreBackend::GetLogging()->LogError(TEXT("Persistence"), Message)`.
**Actual:** The existing `GameCoreBackend.h` stub declares
`static FGameCoreLoggingService& GetLogging(const FGameplayTag&)` (requires a tag argument, returns a
reference, not a pointer) and `FGameCoreLoggingService::LogError(const FString&)` (single argument, no
category).
**Adaptation:** All call sites pass `FGameCoreBackend::GetLogging(FGameplayTag()).LogError(Message)` — an
empty `FGameplayTag` is passed as the tag argument and the category string is embedded in the message.
This is consistent with how other systems in the plugin use the backend stub.

---

## DEV-04: `LogPersistence` log category defined per translation unit

**Spec:** References `LogPersistence` without specifying where it is declared.
**Actual:** `DEFINE_LOG_CATEGORY_STATIC(LogPersistence, Log, All)` is defined locally in both
`PersistenceRegistrationComponent.cpp` and `PersistenceSubsystem.cpp` as static (file-scoped) categories.
**Reason:** No shared logging header for the Persistence module was specified. Static categories avoid
linker conflicts between the two translation units while keeping the category name consistent.

---

## DEV-05: `Deinitialize()` clears all timers

**Spec:** `Deinitialize()` is declared but its body is not specified.
**Actual:** Clears `SaveTimer`, `FullCycleTickTimer`, and `LoadTimeoutTimer` on the world's timer manager.
**Reason:** Without clearing timers, the timer manager may fire callbacks against a destroyed subsystem
during teardown. This is the minimal safe implementation.

---

## DEV-06: `TSet::Add` return value used for duplicate key check

**Spec:** `if (!Keys.Add(Key).IsAlreadyInSet())` — this is the pattern from the spec.
**Actual:** Replaced with `bool bAlreadyInSet = false; Keys.Add(Key, &bAlreadyInSet); if (bAlreadyInSet)`
using the two-argument `TSet::Add` overload.
**Reason:** `TSet::Add` in UE5 returns `TSet<T>::FSetElementId` (not a struct with `IsAlreadyInSet()`).
The correct API to detect pre-existing elements is the `bool*` out-parameter overload.
