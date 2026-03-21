# Stats System — Architecture

**Status:** Active Specification | **UE Version:** 5.7 | **Module:** `GameCore`

---

## Overview

The Stats System is a server-authoritative, data-driven numerical tracker for per-player lifetime statistics (enemies killed, damage dealt, quests completed, etc.). It accumulates `float` values keyed by `FGameplayTag`, persists them via the Serialization System, and broadcasts changes on the Event Bus so downstream systems (Achievement, Quest, Leaderboard) react without any direct coupling.

All mutation is server-side only. Clients hold no write path.

---

## Dependencies

### Unreal Engine Modules

| Module | Usage |
|---|---|
| `GameplayTags` | `FGameplayTag` stat + channel keys |
| `GameplayMessageRuntime` | `UGameplayMessageSubsystem` — channel-based messaging (`FGameplayMessageListenerHandle`) |
| `StructUtils` | `FInstancedStruct` payload in increment rules |
| `Engine` | `UActorComponent`, `UDataAsset`, `APlayerState`, `FTimerHandle` |

### GameCore Plugin Systems

| System | Type | Usage |
|---|---|---|
| Event Bus (`UGameCoreEventBus2`) | **Required** | Auto-listener registration from rules; `FStatChangedEvent` broadcast |
| Serialization (`IPersistableComponent`, `UPersistenceSubsystem`) | **Required** | Dirty-flag flush of `RuntimeValues` |
| Requirement System (`URequirementSet`) | **Optional** | Per-definition increment gate |

### No Dependencies On

- Achievement System (subscribes to Event Bus; no direct dependency)
- Quest System (subscribes to Event Bus; no direct dependency)
- Any game-module code

---

## Requirements

- Stats are identified by `FGameplayTag`; no C++ enum or hard-coded integer IDs.
- Each stat is fully defined in a `UStatDefinition` DataAsset — no C++ required for fixed-increment stats.
- Increment logic is encapsulated in `UStatIncrementRule` (abstract, `EditInlineNew`). Subclasses live in the game module for payload-specific extraction.
- A `URequirementSet` on the definition gates whether an increment applies. Evaluated on the server at increment time.
- One stat can be fed by multiple rules on different channels.
- `UStatComponent` auto-registers Event Bus listeners at `BeginPlay` from authored rules. No game-module wiring subsystem.
- Runtime values live in `UStatComponent` on `APlayerState`, which implements `IPersistableComponent`.
- Persistence uses a dirty-flag + periodic flush (default 10 s) with a forced flush on `EndPlay`.
- Stat changes are broadcast via `FStatChangedEvent` on `Event.Stat.Changed`.
- Server-only mutation. `GetStat` is read-safe client-side.

---

## Features

- **Data-driven**: content authors create stats and wire increment rules entirely in the editor.
- **Zero game-module wiring code**: event-driven stat increments auto-register from the DataAsset at `BeginPlay`.
- **Payload-agnostic core**: `UStatIncrementRule::ExtractIncrement` owns typed extraction; `UStatComponent` stays generic.
- **Gated increments**: optional `URequirementSet` per definition prevents out-of-context accumulation.
- **Decoupled broadcasting**: `FStatChangedEvent` on the Event Bus; downstream systems need no knowledge of `UStatComponent`.
- **Anti-cheat**: zero client write paths anywhere in the system.
- **Efficient persistence**: dirty-flag + flush — no per-increment DB write.
- **Non-shipping validation**: `UStatComponent::ValidateDefinitions()` catches null entries, duplicate tags, and invalid channels at `BeginPlay`.
- **Manual increment path**: `AddToStat()` for non-event-driven cases (quest rewards, login bonuses).

---

## Design Decisions

| Decision | Rationale |
|---|---|
| `FInstancedStruct` payload via Event Bus | Enables auto-registration without compile-time type knowledge in the DataAsset. Rules extract typed data via `GetPtr<T>()`. |
| `UStatIncrementRule::ExtractIncrement` owns extraction | Single override point for C++ and Blueprint subclasses. Keeps `UStatComponent` generic. |
| `UStatComponent` auto-registers listeners | Eliminates a game-module wiring subsystem. All event-driven wiring is data-driven from authored definitions. |
| One listener per `(ChannelTag, StatTag)` pair | Same total call count as deduplication + fan-out. Simpler — no `TMultiMap` bookkeeping. |
| `UStatIncrementRule` as `EditInlineNew` UObject | Fully editor-configured increment logic; one subclass per message shape, reused across stats. |
| Rules live on `UStatDefinition`, not on the component | Stat identity and tracking logic co-located in one authorable asset. |
| `URequirementSet` per definition | Reuses existing Requirement System; no new gating mechanism. |
| Dirty-flag + flush persistence | High-frequency stats (damage, hits) would be too expensive to persist per-increment. |
| Server-only mutation | Anti-cheat. Client has no write path. |
| Event Bus broadcast on change | Decouples Achievement/Quest systems entirely. |
| `Delta <= 0` guard in `AddToStat` | Stats are cumulative monotonic counters; negative increments are a logic error in this system. |
| Requirements loop is O(n) at increment time | Definitions arrays are small (< 50 entries typical); the loop is negligible. A `TMap` cache at `BeginPlay` is noted as a future optimisation if needed. |

---

## Logic Flow

### Event-Driven Increment Path

```
1. UGameCoreEventBus2 message arrives on ChannelTag
2. UStatComponent listener fires (registered at BeginPlay from UStatIncrementRule)
3. UStatIncrementRule::ExtractIncrement(Payload) → float Delta
4. if Delta <= 0: suppress
5. UStatComponent::AddToStat(StatTag, Delta)
   a. HasAuthority() check — no-op on client
   b. Iterate Definitions for matching StatTag
   c. Evaluate URequirementSet::AreRequirementsMet() if set
   d. RuntimeValues[StatTag] += Delta
   e. DirtyStats.Add(StatTag)
   f. Broadcast FStatChangedEvent on Event.Stat.Changed (ServerOnly scope)
```

### Flush Path

```
1. FTimerHandle fires every FlushIntervalSeconds (default 10s)
   OR EndPlay is called
2. UStatComponent::FlushDirtyStats()
3. if DirtyStats.Num() == 0: return
4. UPersistenceSubsystem::RequestFlush(this)
5. DirtyStats.Empty()
6. IPersistableComponent::Serialize_Implementation writes RuntimeValues
```

### Class Interaction

```
APlayerState
  └── UStatComponent (UActorComponent + IPersistableComponent)
        ├── TArray<UStatDefinition*>  Definitions   [authored]
        ├── TMap<FGameplayTag, float> RuntimeValues  [runtime]
        ├── TSet<FGameplayTag>        DirtyStats      [runtime]
        └── TArray<FGameplayMessageListenerHandle> ListenerHandles

UStatDefinition (UDataAsset)
  ├── FGameplayTag                 StatTag
  ├── TArray<UStatIncrementRule*>  Rules          [EditInlineNew]
  └── URequirementSet*             TrackingRequirements

UStatIncrementRule (abstract UObject, EditInlineNew)
  ├── GetChannelTag() → FGameplayTag
  └── ExtractIncrement(FInstancedStruct) → float

UConstantIncrementRule : UStatIncrementRule
  ├── FGameplayTag ChannelTag
  └── float Amount

FStatChangedEvent (USTRUCT)
  ├── FGameplayTag    StatTag
  ├── float           NewValue
  ├── float           Delta
  └── FUniqueNetIdRepl PlayerId
```

---

## Known Issues

1. **O(n) requirements scan per `AddToStat`**: iterates `Definitions` to find the matching definition. Negligible for typical definition counts (< 50), but should be replaced with a `TMap<FGameplayTag, UStatDefinition*>` cache at `BeginPlay` if definitions grow large.

2. **`GetStat` is server-only meaningful**: the spec notes `GetStat` is safe to call client-side, but `RuntimeValues` is not replicated. Client-side reads return 0 for all stats unless the game module adds explicit replication. This is by design (server-authoritative), but must be clearly communicated to game module developers building progress UI.

3. **`FUniqueNetIdRepl` in `FStatChangedEvent`**: using `FUniqueNetIdRepl` assumes Online Subsystem is present. Projects without OSS will need to substitute with a different player identity key (e.g. `APlayerState::GetPlayerName()` or a project-specific GUID). This is not a blocker but is a portability concern for a generic plugin.

4. **No negative delta support**: `AddToStat` guards against `Delta <= 0`. This is correct for cumulative lifetime stats, but means the system cannot be reused for bidirectional tracked values (e.g. current health). This is intentional — the system is for statistics, not attribute values.

5. **`Delta <= 0` in `RegisterListeners` lambda**: the lambda fires `AddToStat` only when `Delta > 0.f`. A rule returning exactly `0.f` is silently ignored. Rules returning negative values (a misuse) are also suppressed, but no warning is emitted. A non-shipping log would help catch badly authored rules.

---

## File Structure

```
GameCore/Source/GameCore/
└── Stats/
    ├── StatComponent.h
    ├── StatComponent.cpp
    ├── StatDefinition.h
    ├── StatDefinition.cpp          (minimal — DataAsset, no logic)
    ├── StatIncrementRule.h
    ├── StatIncrementRule.cpp
    ├── ConstantIncrementRule.h     (can live in StatIncrementRule.h if small)
    └── StatTypes.h                 (FStatChangedEvent)
```
