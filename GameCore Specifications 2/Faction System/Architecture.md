# Faction System — Architecture

**Part of:** GameCore Plugin | **Module:** `GameCore` | **UE Version:** 5.7

The Faction System defines team membership and inter-group relationships for players and NPCs. It provides a single, authoritative relationship query path consumed by AI, combat logic, interaction gating, and UI. The system is fully data-driven and server-authoritative. No game-specific logic is embedded — all wiring (combat, AI, reputation progression) is done in the game module.

---

## Dependencies

### Unreal Engine Modules

| Module | Use |
|---|---|
| `Engine` | `UActorComponent`, `UWorldSubsystem`, `UPrimaryDataAsset` |
| `GameplayTags` | `FGameplayTag`, `FGameplayTagContainer` — faction identity and rank keys |
| `GameplayMessageSubsystem` | Underlying transport for `UGameCoreEventBus` |
| `NetCore` | `FFastArraySerializer`, `FFastArraySerializerItem` — membership replication |
| `DeveloperSettings` | `UDeveloperSettings` for `UGameCoreFactionSettings` |
| `DataValidation` | `IDataValidationInterface`, `FDataValidationContext` for editor validation |

### GameCore Plugin Systems

| System | Use |
|---|---|
| **Event Bus** (`UGameCoreEventBus`) | Broadcasting `MemberJoined`, `MemberLeft`, `RankChanged` events |
| **Requirement System** (`URequirement`, `FRequirementContext`) | `JoinFaction` join-gate evaluation; `URequirement_FactionCompatibility` |

### Optional / Game-Module Dependencies

| System | Use |
|---|---|
| **Progression System** (`ULevelProgressionDefinition`) | Soft reference only — `UFactionDefinition::ReputationProgression`. GameCore does not wire this. |

---

## Requirements

- Attach multiple faction memberships to any actor (player or NPC) via a single component.
- Resolve the relationship between two actors from their combined faction memberships.
- Configure explicit faction-to-faction relationships in a designer-authored data asset.
- Configure a per-faction default relationship used when no explicit pair entry exists.
- Configure a per-component fallback relationship used when an actor has no factions.
- Relationship enum has five ordered values: `Hostile (0)`, `Unfriendly (1)`, `Neutral (2)`, `Friendly (3)`, `Ally (4)`. Ordering is load-bearing.
- When two factions have no explicit relationship, resolve via `FMath::Min` of their two `DefaultRelationship` uint8 values (least-friendly wins).
- Cache all explicit relationships at world startup into a flat `TMap` for O(1) runtime lookup.
- Support join requirements on factions, evaluated server-side via the Requirement System.
- Support per-entity local relationship overrides that take precedence over the global cache.
- Broadcast `UGameCoreEventBus` events when faction membership or rank changes.
- Faction memberships are divided into **primary** (participate in relationship resolution) and **secondary** (grouping/affiliation only — never checked for relationship).
- No data is persisted by this system. All runtime faction state is derived at load time or set by the game module.

---

## Features

- **O(1) relationship queries** — explicit pairs cached at startup; cache-miss resolution is a single `FMath::Min` with no further lookup.
- **Multi-faction actors** — an actor with N primary factions vs M primary factions evaluates all N×M pairs and returns the minimum (worst) relationship.
- **Local overrides** — per-component `LocalOverrides` array checked before global cache; used for bounty hunters, story NPCs, and scripted encounters.
- **Secondary memberships** — grouping tags (guild, crew, bounty status) that have zero impact on relationship resolution.
- **Join requirements** — `UFactionDefinition::JoinRequirements` uses the existing Requirement System; no new gate mechanism needed.
- **Rank tracking** — `FFactionMembership::RankTag` tracks current rank; validated against `UFactionDefinition::RankTags` in non-shipping builds.
- **Reputation link hook** — `UFactionDefinition::ReputationProgression` is a soft reference; wiring to progression events is done entirely in the game module.
- **Designer-friendly** — all configuration lives in two `UPrimaryDataAsset` types; no code changes needed to add factions.
- **Full replication** — memberships replicate delta-compressed via `FFastArraySerializer`; overrides and fallback replicate in full (expected small).

---

## Design Decisions

| Decision | Rationale |
|---|---|
| Cache stores only explicit pair overrides | Pre-populating the full N² matrix scales poorly. Default resolution is O(1) arithmetic on cache miss — no pre-population needed. |
| Sorted-pair TMap key (`FFactionSortedPair`) | `GetRelationship(A,B)` must equal `GetRelationship(B,A)`. Sorting by `FName::LexicalLess` at construction gives order-independence with a single map entry. |
| `uint8` min for default resolution | `EFactionRelationship` values are ordered `0..4`. `FMath::Min` on two `uint8` casts gives the least-friendly result with zero branching. |
| Join constraint is a `URequirement` subclass | Keeps the enum as pure data. `URequirement_FactionCompatibility` lets each faction set its own conflict threshold independently. |
| Local overrides on `UFactionComponent` | Per-entity exceptions don't pollute the global relationship table. |
| Rank tag per membership | Rank affects downstream systems via the existing Requirement System — no new query API needed. |
| Events via `UGameCoreEventBus`, not delegates | AI, interaction, and UI react without polling. The component never imports those consumers. |
| No persistence in this system | Player state is saved by the game module. NPCs have memberships set in Blueprint defaults or spawn config. |
| Reputation wiring is NOT in GameCore | Rank advancement policy (every level, every 5, threshold-based) is game-specific. GameCore ships the hooks; the game module owns the listener. |
| Secondary factions never checked for relationship | Including grouping tags in resolution would produce false hostility between unrelated tags. |
| `UFactionRelationshipTable` is a singleton | One table per project, configured in `UGameCoreFactionSettings`. Avoids merge conflicts between tables and keeps validation deterministic. |
| `FactionTag` is always the runtime key | All subsystem lookups use `FactionTag` directly even when `Faction` soft pointer is set. Avoids soft-pointer loads on the hot query path. |

---

## Logic Flow

```
[Designer Authoring]
  UFactionDefinition          ← one asset per faction (identity, default, ranks, join reqs)
  UFactionRelationshipTable   ← singleton project asset (all factions + explicit pairs)
  UGameCoreFactionSettings    ← Project Settings → GameCore → Factions (table path)

[World Start — UFactionSubsystem::OnWorldBeginPlay]
  → Load UFactionRelationshipTable via UGameCoreFactionSettings
  → Load all UFactionDefinition assets listed in table
  → BuildCache():
      - DefinitionMap:           FactionTag → UFactionDefinition*
      - RelationshipCache:       FFactionSortedPair → EFactionRelationship  (explicit pairs only)
      - ReputationProgressionMap: ProgressionTag → FactionTag              (for game module wiring)
  → ValidateTable() [non-shipping]

[Actor Spawn — UFactionComponent::BeginPlay]
  → [non-shipping] Warn if any primary FactionTag has no registered UFactionDefinition

[Mutation — Server Only]
  UFactionComponent::JoinFaction(FactionTag)
    → HasAuthority() guard
    → IsMemberOf() early return (idempotent)
    → UFactionSubsystem::GetDefinition(FactionTag)  → UFactionDefinition*
    → Evaluate JoinRequirements via FRequirementContext
    → Memberships.Items.AddDefaulted_GetRef() + MarkItemDirty()
    → UGameCoreEventBus::Broadcast(Faction_MemberJoined, ServerOnly)
    → OnMembershipChanged.Broadcast()

  UFactionComponent::LeaveFaction(FactionTag)
    → HasAuthority() guard
    → Find + RemoveAtSwap + MarkArrayDirty()
    → UGameCoreEventBus::Broadcast(Faction_MemberLeft, ServerOnly)
    → OnMembershipChanged.Broadcast()

  UFactionComponent::SetRank(FactionTag, RankTag)
    → HasAuthority() guard
    → Find membership + update RankTag + MarkItemDirty()
    → [non-shipping] ensure RankTag in UFactionDefinition::RankTags
    → UGameCoreEventBus::Broadcast(Faction_RankChanged, ServerOnly)
    → OnRankChanged.Broadcast()

[Query — Server and Client]
  UFactionComponent::GetRelationshipTo(Other)
    → UFactionSubsystem::GetActorRelationship(Source, Target)
        → Collect primary FactionTags from both components
        → If either has none → FMath::Min(Source.Fallback, Target.Fallback)
        → For each (SF, TF) pair:
            → CheckLocalOverrides(Source, Target, SF, TF)  — check both components
            → On miss: UFactionSubsystem::GetRelationship(SF, TF)
                → Self-check (SF == TF) → Ally
                → RelationshipCache.Find(SortedPair) → explicit value
                → Cache miss → ResolveDefault(SF, TF) → FMath::Min(DefaultA, DefaultB)
            → Track minimum across all pairs
            → Short-circuit on Hostile
        → Return minimum
```

---

## Known Issues

| # | Issue | Severity | Notes |
|---|---|---|---|
| 1 | `GetActorRelationship` fallback logic is convoluted | Low | When one actor has no primary factions the min-fallback expression is verbose and confusing. Should be extracted to a helper. |
| 2 | `GetHostileFactions` iterates all factions for every NPC that calls it | Medium | Fine at low faction counts but not cache-friendly. A pre-built reverse map (faction → hostile factions) would be faster for large worlds. See Code Review. |
| 3 | No runtime cache invalidation path | Low | `RelationshipCache` is immutable after startup by design. If designers need to hot-reload faction data during PIE they must restart the world. |
| 4 | `LocalOverrides` replicates in full with no size cap enforced at runtime | Low | Expected to be 0–3 entries. Could degrade if misused. |
| 5 | `FindFactionByReputationProgression` requires loading `ULevelProgressionDefinition` synchronously at startup | Low | Acceptable at startup; noted for future soft-reference lazy approach. |

---

## File Structure

```
GameCore/Source/GameCore/
└── Factions/
    ├── FactionTypes.h                           ← EFactionRelationship, FFactionMembership,
    │                                               FFactionMembershipArray, FFactionSortedPair,
    │                                               FFactionRelationshipOverride,
    │                                               FFactionMembershipChangedMessage
    ├── FactionDefinition.h / .cpp               ← UFactionDefinition
    ├── FactionRelationshipTable.h / .cpp        ← UFactionRelationshipTable
    ├── FactionDeveloperSettings.h / .cpp        ← UGameCoreFactionSettings
    ├── FactionSubsystem.h / .cpp                ← UFactionSubsystem
    ├── FactionComponent.h / .cpp               ← UFactionComponent
    └── Requirements/
        └── Requirement_FactionCompatibility.h / .cpp

GameCore/Source/GameCore/GameCore.Build.cs
  → Add: "GameplayTags", "GameplayMessageSubsystem", "NetCore", "DeveloperSettings"

Config/DefaultGameplayTags.ini
  +GameplayTagList=(Tag="GameCoreEvent.Faction.MemberJoined")
  +GameplayTagList=(Tag="GameCoreEvent.Faction.MemberLeft")
  +GameplayTagList=(Tag="GameCoreEvent.Faction.RankChanged")
  +GameplayTagList=(Tag="Faction")
```

---

## Reputation / Rank Wiring Guide

> ⚠️ This wiring is **NOT implemented in GameCore**. The following is a guide for game module developers. GameCore ships the hooks (`SetRank`, `OnRankChanged`). The listener and mapping logic must be authored in the game module.

GameCore's Progression System fires `GameCoreEvent.Progression.LevelChanged` on the event bus when a tracked progression level changes. If a `UFactionDefinition` has `ReputationProgression` set, the game module can listen for that event and map progression levels to faction rank tags.

```cpp
// In AMyPlayerState::BeginPlay() or a dedicated component:
UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this);
MemberJoinedHandle = Bus->StartListening<FProgressionLevelChangedMessage>(
    GameCoreEventTags::Progression_LevelChanged,
    [this](FGameplayTag, const FProgressionLevelChangedMessage& Msg)
    {
        UFactionSubsystem* FS = GetWorld()->GetSubsystem<UFactionSubsystem>();
        FGameplayTag FactionTag = FS->FindFactionByReputationProgression(Msg.ProgressionTag);
        if (!FactionTag.IsValid()) return;

        const UFactionDefinition* Def = FS->GetDefinition(FactionTag);
        if (!Def || Def->RankTags.IsEmpty()) return;

        int32 RankIndex = FMath::Clamp(
            (Msg.NewLevel - 1) * Def->RankTags.Num() / Def->MaxReputationLevel,
            0, Def->RankTags.Num() - 1);

        UFactionComponent* FC = FindComponentByClass<UFactionComponent>();
        if (FC) FC->SetRank(FactionTag, Def->RankTags[RankIndex]);
    });
```

No part of this flow is automatic. `UFactionSubsystem` does not listen to progression events.
