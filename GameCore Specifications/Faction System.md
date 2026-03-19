# Faction System

**Part of: GameCore Plugin** | **Status: Active Specification** | **UE Version: 5.7**

The Faction System defines team membership and inter-group relationships for players and NPCs. It provides a single, authoritative relationship query path used by AI, combat, interaction gating, and UI. The system is fully data-driven and server-authoritative. No game-specific logic is embedded — all wiring to game systems (combat, AI, reputation progression) is done in the game module.

---

# Requirements

The following are the hard requirements this system was designed to satisfy:

- Attach multiple faction memberships to any actor (player or NPC) via a single component.
- Resolve the relationship between two actors from their combined faction memberships.
- Configure explicit faction-to-faction relationships in a designer-authored data asset.
- Configure a per-faction default relationship used when no explicit pair entry exists.
- Configure a per-component fallback relationship used when an actor has no factions at all.
- Relationship enum has five values, ordered least-to-most friendly: `Hostile`, `Unfriendly`, `Neutral`, `Friendly`, `Ally`.
- When two factions have no explicit relationship, resolve via the minimum (least friendly) of their two default relationships.
- Cache all explicit relationships at world startup into a flat `TMap` for O(1) runtime lookup.
- Support join requirements on factions, evaluated via the existing Requirement System.
- Support per-entity relationship overrides that take precedence over the global cache.
- Broadcast GMS events when faction membership changes.
- Faction memberships are divided into **primary** (participate in relationship resolution) and **secondary** (grouping/affiliation only, never checked for relationship).
- No data is persisted by this system. All runtime faction state is derived at load time or recomputed on relevant events.

---

# Key Design Decisions

| Decision | Rationale |
|---|---|
| Cache stores only explicit pair overrides | Pre-populating the full N² matrix scales poorly. Default resolution is O(1) arithmetic on a cache miss — no pre-population needed. |
| Sorted-pair TMap key | Relationship between A and B must equal B and A. Sorting tags by name at construction makes the pair order-independent with a single entry in the map. |
| `uint8` min for default resolution | `EFactionRelationship` values are ordered `0..4`. `FMath::Min` on the two `uint8` casts gives the least-friendly result with zero branching or lookup. |
| Join constraint is a `URequirement`, not an enum flag | Keeping the relationship enum as pure data and expressing mutual-exclusion as a configurable `URequirement_FactionCompatibility` allows each faction to set its own threshold (hostile, unfriendly, etc.) independently. |
| Local overrides on `UFactionComponent` | Per-entity exceptions (bounty hunters, story NPCs) don't pollute the global relationship table. Override resolution is checked before the subsystem cache. |
| Rank tag per membership | Rank affects downstream systems (interaction gating, AI dialogue) via the existing Requirement System — no new query API needed. |
| GMS events on join/leave/rank change | AI, interaction, and UI react without polling. The component never imports those consumers. |
| No persistence in this system | Faction membership for players is loaded from save data by the game module. NPCs have their memberships set in Blueprint defaults or spawn config. This system stores runtime state only. |
| Reputation progression wiring is NOT done here | Rank advancement driven by reputation XP is game-specific wiring. GameCore ships delegate hooks and a wiring guide. The game module owns the listener. |
| Secondary factions not checked for relationship | Secondary memberships (ship crew, guild, bounty status) are grouping/affiliation data. Including them in relationship resolution would produce false hostility between unrelated tags. |

---

# System Units

| Unit | Class | Lives On |
|---|---|---|
| Relationship enum & types | `EFactionRelationship`, `FFactionMembership`, etc. | `FactionTypes.h` |
| Faction definition | `UFactionDefinition` | `UPrimaryDataAsset` — one per faction |
| Global relationship table | `UFactionRelationshipTable` | `UPrimaryDataAsset` — singleton per project |
| Runtime cache + query | `UFactionSubsystem` | `UWorldSubsystem` |
| Actor membership + queries | `UFactionComponent` | `UActorComponent` on players and NPCs |
| Join constraint | `URequirement_FactionCompatibility` | `URequirement` subclass in GameCore |

---

# How the Pieces Connect

```
[Designer]
  UFactionDefinition          ← one asset per faction
  UFactionRelationshipTable   ← single project-wide asset; lists all factions and explicit pairs

[World Start]
  UFactionSubsystem::OnWorldBeginPlay
    → loads UFactionRelationshipTable (via UGameCoreDeveloperSettings)
    → loads all UFactionDefinition assets listed in the table
    → builds RelationshipCache (explicit pairs only)
    → builds DefinitionMap (FactionTag → UFactionDefinition)
    → calls ValidateTable() in non-shipping builds

[Runtime Query]
  UFactionComponent::GetRelationshipTo(Other)
    → check this->LocalOverrides for any pair
    → check Other->LocalOverrides for any pair
    → call UFactionSubsystem::GetActorRelationship(this, Other)
        → iterate primary memberships of both actors
        → for each faction pair: check RelationshipCache; on miss, call ResolveDefault()
        → return minimum across all pairs

[Join]
  UFactionComponent::JoinFaction(FactionTag)
    → find UFactionDefinition from DefinitionMap
    → evaluate JoinRequirements (server only)
    → add FFactionMembership to Memberships
    → broadcast GameCoreEvent.Faction.MemberJoined via GMS
```

---

# Relationship Resolution Rules

**Explicit pair present in cache:** return the cached value directly.

**No explicit pair (cache miss):** return `FMath::Min(DefaultA, DefaultB)` where each default is the `uint8` cast of `UFactionDefinition::DefaultRelationship`. When one or both factions have no `UFactionDefinition`, fall back to the `UFactionComponent::FallbackRelationship` of the querying component.

**Multi-faction actors (primary only):** when Actor A has N primary factions and Actor B has M primary factions, evaluate all N×M pairs. Return the minimum relationship across all pairs.

**Local overrides (component level):** checked before the subsystem cache. If any local override on either component covers a relevant pair, that value is used and cache lookup is skipped for that pair.

**Secondary factions:** never included in relationship resolution. They exist for grouping, filtering, and downstream systems only.

---

# Quick Setup

**Configuring factions for a project:**

1. Create one `UFactionDefinition` asset per faction. Set `FactionTag`, `DisplayName`, `DefaultRelationship`, and optionally `JoinRequirements` and `RankTags`.
2. Create one `UFactionRelationshipTable` asset. Add all faction definitions to `Factions`. Add explicit pair entries to `ExplicitRelationships`.
3. Open **Project Settings → GameCore** and assign the table to `FactionRelationshipTable`.
4. On world start, `UFactionSubsystem` builds the cache automatically — no code call needed.

**Attaching factions to an NPC:**

1. Add `UFactionComponent` to the NPC Blueprint.
2. In the Details panel, add entries to `Memberships`. Set `Faction` (soft pointer to a `UFactionDefinition`), optional `RankTag`, and set `bPrimary = true`.
3. Set `FallbackRelationship` on the component (used when `Memberships` is empty).

**Querying relationship at runtime (C++):**

```cpp
// On server or client (primary memberships only):
UFactionComponent* SourceFC = Source->FindComponentByClass<UFactionComponent>();
UFactionComponent* TargetFC = Target->FindComponentByClass<UFactionComponent>();

if (SourceFC && TargetFC)
{
    EFactionRelationship Rel = SourceFC->GetRelationshipTo(TargetFC);
    if (Rel == EFactionRelationship::Hostile)
    {
        // engage combat
    }
}
```

**Joining a faction (server only):**

```cpp
FText FailureReason;
if (!FactionComponent->JoinFaction(FactionTags::Navy, FailureReason))
{
    // send failure reason to client UI
}
```

---

# Reputation / Rank Wiring Guide

> ⚠️ **This wiring is NOT implemented in GameCore.** The following is a guide for game module developers. GameCore ships the hooks (`SetRank`, `OnReputationLevelChanged`). The listener and mapping logic must be authored in the game module.

GameCore's Progression System fires `GameCoreEvent.Progression.LevelChanged` on the GMS when a tracked progression level changes. If a `UFactionDefinition` has `ReputationProgression` set (pointing to a `ULevelProgressionDefinition` such as `DA_Progression_NavyReputation`), the game module can listen for that event and map progression levels to faction rank tags.

**Suggested wiring in the game module:**

```cpp
// In AMyPlayerState::BeginPlay() or a dedicated component:
UGameCoreEventSubsystem* Bus = GetWorld()->GetSubsystem<UGameCoreEventSubsystem>();
Bus->RegisterListener(
    GameCoreEventTags::Progression_LevelChanged,
    this,
    &AMyPlayerState::OnProgressionLevelChanged);

void AMyPlayerState::OnProgressionLevelChanged(
    const FProgressionLevelChangedMessage& Msg)
{
    // Check if this progression tag maps to a faction reputation
    UFactionSubsystem* FS = GetWorld()->GetSubsystem<UFactionSubsystem>();
    FGameplayTag FactionTag = FS->FindFactionByReputationProgression(Msg.ProgressionTag);
    if (!FactionTag.IsValid()) return;

    // Map progression level to a rank tag using the faction's RankTags array
    const UFactionDefinition* Def = FS->GetDefinition(FactionTag);
    if (!Def || Def->RankTags.IsEmpty()) return;

    // Example: divide levels evenly across rank tiers
    int32 RankIndex = FMath::Clamp(
        (Msg.NewLevel - 1) * Def->RankTags.Num() / Def->MaxReputationLevel,
        0, Def->RankTags.Num() - 1);
    FGameplayTag NewRank = Def->RankTags[RankIndex];

    UFactionComponent* FC = FindComponentByClass<UFactionComponent>();
    if (FC) FC->SetRank(FactionTag, NewRank);
}
```

The delegate `UFactionComponent::OnReputationLevelChanged` is also provided for Blueprint-based wiring. Broadcast it from game code after calling `SetRank` if downstream systems need to react.

**No part of this flow is automatic.** `UFactionSubsystem` does not listen to progression events. `UFactionComponent` does not listen to progression events. This is intentional — rank advancement policy (every level, every 5 levels, threshold-based) is game-specific.

---

# GMS Event Tags

```ini
; DefaultGameplayTags.ini
+GameplayTagList=(Tag="GameCoreEvent.Faction.MemberJoined")
+GameplayTagList=(Tag="GameCoreEvent.Faction.MemberLeft")
+GameplayTagList=(Tag="GameCoreEvent.Faction.RankChanged")

; Faction identity namespace
+GameplayTagList=(Tag="Faction")
```

---

# Sub-Pages

[FactionTypes — Enums, Structs, Hashing](Faction%20System/FactionTypes.md)

[UFactionDefinition & UFactionRelationshipTable](Faction%20System/UFactionDefinition%20and%20UFactionRelationshipTable.md)

[UFactionSubsystem](Faction%20System/UFactionSubsystem.md)

[UFactionComponent & URequirement_FactionCompatibility](Faction%20System/UFactionComponent%20and%20Requirement.md)

---

# File and Folder Structure

```
GameCore/Source/GameCore/
└── Factions/
    ├── FactionTypes.h                          ← EFactionRelationship, FFactionMembership,
    │                                              FFactionSortedPair, FFactionRelationshipOverride,
    │                                              FFactionMembershipChangedMessage
    ├── FactionDefinition.h / .cpp              ← UFactionDefinition
    ├── FactionRelationshipTable.h / .cpp       ← UFactionRelationshipTable
    ├── FactionSubsystem.h / .cpp              ← UFactionSubsystem
    ├── FactionComponent.h / .cpp              ← UFactionComponent
    ├── FactionDeveloperSettings.h / .cpp      ← UGameCoreFactionSettings (UDeveloperSettings)
    └── Requirements/
        └── Requirement_FactionCompatibility.h / .cpp
```

---

# Implementation Constraints

- `UFactionSubsystem` is a `UWorldSubsystem`. The cache is rebuilt on every `OnWorldBeginPlay` — no stale data between PIE sessions.
- `JoinFaction` and `LeaveFaction` are **server-only**. Memberships replicate to clients via `FFastArraySerializer` on `UFactionComponent`.
- `GetRelationshipTo` is callable on both server and client — it reads only replicated membership data and the in-memory subsystem cache.
- Secondary memberships (`bPrimary = false`) must never appear in relationship resolution. Any code path calling `GetActorRelationship` or `GetHostileFactions` must filter to primary memberships only.
- `UFactionDefinition::JoinRequirements` must contain only synchronous requirements. Validated at `BeginPlay` via `URequirementLibrary::ValidateRequirements` in non-shipping builds.
- `UFactionRelationshipTable` is a singleton. Only one table asset is active per project, configured in `UGameCoreFactionSettings`. Multiple tables are not supported.
- Rank tags set on `FFactionMembership::RankTag` must be a member of `UFactionDefinition::RankTags`. Validated in non-shipping builds by `UFactionComponent::SetRank`.
- No data is serialized or persisted by this system. The game module is responsible for saving and restoring player faction state.
