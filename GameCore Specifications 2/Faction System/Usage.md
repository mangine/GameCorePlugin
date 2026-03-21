# Faction System — Usage

---

## Project Setup

1. Create one `UFactionDefinition` asset per faction. Set `FactionTag`, `DisplayName`, `DefaultRelationship`, and optionally `JoinRequirements` and `RankTags`.
2. Create one `UFactionRelationshipTable` asset. Add all faction definitions to `Factions`. Add explicit pair entries to `ExplicitRelationships`.
3. Open **Project Settings → GameCore → Factions** and assign the table.
4. `UFactionSubsystem` builds the relationship cache automatically on `OnWorldBeginPlay` — no code call needed.

---

## Attaching Factions to an NPC (Blueprint)

1. Add `UFactionComponent` to the NPC Blueprint.
2. In the Details panel, add entries to `Memberships`. Set `FactionTag`, optional `RankTag`, and `bPrimary = true`.
3. Set `FallbackRelationship` (used when `Memberships` is empty).

---

## Querying Relationship (C++)

```cpp
// Safe on server and client — reads replicated membership data.
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

---

## Querying Relationship (Blueprint)

Call **Get Relationship To** on the source `UFactionComponent`, passing the target `UFactionComponent`. Returns `EFactionRelationship`.

---

## Joining a Faction (Server Only)

```cpp
// Must be called on the server.
FText FailureReason;
if (!FactionComponent->JoinFaction(FactionTags::Navy, FailureReason))
{
    // Send FailureReason to the owning client's UI.
}
```

Joining is idempotent — calling `JoinFaction` on a faction the actor is already in returns `true` silently.

---

## Leaving a Faction (Server Only)

```cpp
FactionComponent->LeaveFaction(FactionTags::Navy);
```

---

## Setting a Rank (Server Only)

```cpp
// RankTag must be one of UFactionDefinition::RankTags, or empty.
FactionComponent->SetRank(FactionTags::Navy, FactionRankTags::Navy_Officer);
```

---

## Checking Membership

```cpp
if (FactionComponent->IsMemberOf(FactionTags::Pirates_BlackSails))
{
    // ...
}
```

---

## Getting All Faction Tags

```cpp
FGameplayTagContainer Tags;
// bPrimaryOnly = true excludes secondary/grouping memberships.
FactionComponent->GetFactionTags(Tags, /*bPrimaryOnly=*/ true);
```

---

## Direct Faction Pair Query (Bypassing Components)

```cpp
// When you only have tags and no actors.
UFactionSubsystem* FS = GetWorld()->GetSubsystem<UFactionSubsystem>();
EFactionRelationship Rel = FS->GetRelationship(FactionTags::Navy, FactionTags::Pirates_BlackSails);
```

---

## Getting All Hostile Factions for an Actor

Useful for AI targeting and perception systems.

```cpp
UFactionSubsystem* FS = GetWorld()->GetSubsystem<UFactionSubsystem>();
TArray<FGameplayTag> HostileFactions;
FS->GetHostileFactions(FactionComponent, HostileFactions);
// Cache HostileFactions on the AI; refresh on Faction.MemberJoined / MemberLeft events.
```

---

## Adding a Local Relationship Override

For per-entity exceptions (bounty hunters, scripted story encounters) that shouldn't affect the global table.

```cpp
// On the server, directly modify LocalOverrides (replicated array).
FFactionRelationshipOverride Override;
Override.FactionA       = FactionTags::Navy;
Override.FactionB       = FactionTags::BountyHunters;
Override.Relationship   = EFactionRelationship::Hostile;
FactionComponent->LocalOverrides.Add(Override);
```

---

## Configuring a Join Requirement (Designer)

In `UFactionDefinition`, add a `URequirement_FactionCompatibility` to `JoinRequirements`:

- Set `TargetFactionTag` to this faction's tag.
- Set `MinimumAllowedRelationship` to `Unfriendly` to block anyone already hostile to this faction from joining. Set it to `Neutral` to block anyone with any conflict.

Multiple requirements can be stacked (all must pass).

---

## Listening for Membership Events

```cpp
// In BeginPlay — store the handle, stop in EndPlay.
UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this);
MemberJoinedHandle = Bus->StartListening<FFactionMembershipChangedMessage>(
    GameCoreEventTags::Faction_MemberJoined,
    [this](FGameplayTag, const FFactionMembershipChangedMessage& Msg)
    {
        // React to any actor joining a faction.
    });

// In EndPlay:
if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    Bus->StopListening(MemberJoinedHandle);
```

---

## Loading Player Faction State at Login

The Faction System does not persist or auto-restore membership. The game module is responsible.

```cpp
// In AMyPlayerState::OnLoginComplete() or equivalent, after loading save data:
UFactionComponent* FC = FindComponentByClass<UFactionComponent>();
if (!FC) return;

for (const FSavedFactionEntry& Entry : SaveData.Factions)
{
    FText Unused;
    FC->JoinFaction(Entry.FactionTag, Unused, Entry.bPrimary);
    if (Entry.RankTag.IsValid())
        FC->SetRank(Entry.FactionTag, Entry.RankTag);
}
```
