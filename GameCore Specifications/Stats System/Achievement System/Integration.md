# Achievement System — Integration & Events

**Sub-page of:** [Achievement System](./Achievement%20System.md)

---

## FAchievementUnlockedEvent

Broadcast on `UGameCoreEventBus2` (server scope) immediately when an achievement is earned.

```cpp
// AchievementTypes.h
USTRUCT(BlueprintType)
struct GAMECORE_API FAchievementUnlockedEvent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FGameplayTag AchievementTag;
    UPROPERTY(BlueprintReadOnly) FUniqueNetIdRepl PlayerId;
};
```

**Channel tag:** `GameCoreEvent.Achievement.Unlocked`  
**Scope:** `EGameCoreEventScope::Server`

---

## Data Flow Overview

```
[UStatComponent]
  FStatChangedEvent →
    [UAchievementComponent::OnStatChanged]
      → lookup StatToAchievements[StatTag]
      → EvaluateAchievement(Def)
          → CheckStatThresholds()            // reads UStatComponent::GetStat()
          → if AdditionalRequirements: check watcher cached result
          → GrantAchievement()
              → EarnedAchievements.AddTag()
              → MarkDirty() → IPersistableComponent flush
              → UnregisterWatcher()
              → FAchievementUnlockedEvent → GameCoreEvent.Achievement.Unlocked
              → DOREPLIFETIME → owning client OnRep_EarnedAchievements

[URequirementWatcherComponent]
  OnWatcherDirty(bAllPassed=true) →
    [UAchievementComponent::OnWatcherDirty]
      → CheckStatThresholds()
      → GrantAchievement()
```

---

## Wiring Responsibilities

| Responsibility | Owner |
|---|---|
| `UStatDefinition.AffectedAchievements` populated | Content author |
| `UAchievementComponent.Definitions` populated | Content author (APlayerState Blueprint) |
| `URequirementWatcherComponent` present on APlayerState | Game module (required if any achievement uses `AdditionalRequirements`) |
| `InjectRequirementPayload` called for persisted requirements | Game module bridge component |
| `FAchievementUnlockedEvent` consumers (rewards, UI, audio) | Game module |

---

## Dependency Summary

| Dependency | Type |
|---|---|
| Stats System (`UStatComponent`, `FStatChangedEvent`) | Required — stat value reads + change events |
| Event Bus 2 (`UGameCoreEventBus2`) | Required — stat change listener + unlock broadcast |
| Requirement System (`URequirementWatcherComponent`, `URequirementList`) | Optional — only when `AdditionalRequirements` is non-null |
| Serialization System (`IPersistableComponent`) | Required — earned set + payload persistence |

---

## Adding a New Achievement — Checklist

1. **Create** `UAchievementDefinition` DataAsset. Set `AchievementTag`, `StatThresholds`, and optionally `AdditionalRequirements`.
2. **Link stats**: for each `StatTag` in `StatThresholds`, open the corresponding `UStatDefinition` and add the new asset to `AffectedAchievements`.
3. **Register**: add the new `UAchievementDefinition` to `UAchievementComponent.Definitions` on the `APlayerState` Blueprint.
4. **If `AdditionalRequirements` is set**: ensure `URequirementWatcherComponent` is present on `APlayerState`.
5. **If `URequirement_Persisted` subclasses are used**: implement a game module bridge that calls `InjectRequirementPayload` when the relevant events occur.
6. *(Optional)* Subscribe to `GameCoreEvent.Achievement.Unlocked` in the game module for rewards or UI.

---

## Interactions With Other Systems

### Progression System

`FAchievementUnlockedEvent` can be consumed by a game module bridge to grant XP or points:

```cpp
Bus->StartListening<FAchievementUnlockedEvent>(
    TAG_GameCoreEvent_Achievement_Unlocked, this,
    [this](FGameplayTag, const FAchievementUnlockedEvent& Evt)
    {
        ProgressionSubsystem->GrantXP(
            GetPlayerStateForId(Evt.PlayerId), nullptr,
            TAG_Progression_Character, 500, 1, AchievementXPSource);
    });
```

### Journal System

```cpp
Bus->StartListening<FAchievementUnlockedEvent>(
    TAG_GameCoreEvent_Achievement_Unlocked, this,
    [this](FGameplayTag, const FAchievementUnlockedEvent& Evt)
    {
        UJournalComponent* Journal = GetJournalFor(Evt.PlayerId);
        Journal->AddEntry(Evt.AchievementTag, TAG_Journal_Track_Achievements, false);
    });
```

### UI (Client-Side Progress Display)

`GetProgress` is callable client-side only if `UStatComponent::GetStat` is safe on the client. Because `UStatComponent` runtime values are server-only, **progress display requires a separate replicated property or an RPC** in the game module. GameCore does not prescribe this; the game module owns client-facing stat exposure.

---

## Persisted Requirement Payload — Lifecycle

```
BeginPlay
  → payload loaded from persistence into RequirementPayloads
  → watcher registered with ContextBuilder referencing RequirementPayloads

InjectRequirementPayload called
  → RequirementPayloads[AchTag] updated
  → MarkDirty()
  → Watcher::ForceFlush → OnWatcherDirty

GrantAchievement called
  → RequirementPayloads.Remove(AchTag)  // cleaned up
  → WatcherHandle unregistered
  → MarkDirty()

Flush (IPersistableComponent)
  → EarnedAchievements serialized
  → RequirementPayloads serialized (sparse; empty after all grants)
```
