# GameCore Module Dependencies

`A → B` means A depends on B. Optional dependencies are marked with `(optional)`.

---

## UE / External Foundations

These are not GameCore systems — they are prerequisites provided by Unreal Engine or the game project.

```
GameplayTags           — required by all GameCore systems
GameplayMessageSubsystem (GMS)  — required by Event Bus
AbilitySystemComponent (GAS)   — optional, game-side only
```

---

## Layer 1 — Infrastructure

```
Backend      → GameplayTags
Event Bus    → GameplayTags
             → GameplayMessageSubsystem (GMS)
Tags System  → GameplayTags
             → GAS (optional, forwarding actors only)
```

---

## Layer 2 — Persistence

```
Serialization System → GameplayTags
                     → Backend  (logging via FGameCoreBackend)
```

---

## Layer 3 — Logic

```
Requirement System → GameplayTags
                   → Event Bus  (watcher dirty events via GMS)
                   → Tags System  (ITaggedInterface on requirements)
```

---

## Layer 4 — Feature Systems

```
Interaction System  → GameplayTags
                    → Tags System  (ITaggedInterface on actors)
                    → Requirement System (optional, entry requirements)

State Machine       → GameplayTags
                    → Event Bus  (broadcasts StateChanged, TransitionBlocked)
                    → Tags System (optional, GrantedTags on state nodes)

Progression System  → GameplayTags
                    → Event Bus  (broadcasts LevelUp, XPChanged, PointPoolChanged)
                    → Serialization System  (IPersistableComponent)
                    → Requirement System (optional, progression prerequisites)
                    → Backend (optional, audit via FGameCoreBackend)
                    → GAS (optional, personal XP multiplier attribute)
```

---

## Layer 5 — Game Code / Game Plugins

```
Game Code → any feature system it needs
          → watcher adapter listens to GameCoreEvent.Progression.LevelUp via GMS
            and calls URequirementWatcherManager::NotifyPlayerEvent
```

No feature system depends on another feature system. They communicate exclusively through GMS event channels.

---

## Minimum Requirements Per System

| System | Hard dependencies | Optional |
| --- | --- | --- |
| Tags System | `GameplayTags` | GAS |
| Backend | `GameplayTags` | — |
| Event Bus | `GameplayTags`, `GMS` | — |
| Serialization | `GameplayTags`, Backend | — |
| Requirement System | `GameplayTags`, Event Bus, Tags System | — |
| State Machine | `GameplayTags`, Event Bus | Tags System |
| Progression | `GameplayTags`, Event Bus, Serialization | Requirement System, Backend, GAS |
| Interaction | `GameplayTags`, Tags System | Requirement System |

---

## Recommended Adoption Order

```
Tags System → Backend → Event Bus → Serialization → Requirement System
           → then any feature system in any order
```
