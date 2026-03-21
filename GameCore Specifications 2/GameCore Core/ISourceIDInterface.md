# ISourceIDInterface

**Part of:** GameCore Plugin | **Module:** `GameCore` | **Layer:** Core (no GameCore dependencies)

---

## Purpose

`ISourceIDInterface` is a generic GameCore interface that any `UObject` can implement to declare itself as an identifiable source. It is intentionally broad — not tied to XP, drops, or any specific system. Any system that needs to record *where something came from* (XP grants, item drops, market transactions, quest rewards, etc.) accepts a `TScriptInterface<ISourceIDInterface>` and logs a structured identity without coupling to a concrete class.

---

## File Location

```
GameCore/Source/GameCore/Core/SourceID/SourceIDInterface.h
```

Header-only — no `.cpp` required.

---

## Design Notes

- **No system dependency** — lives at the absolute bottom of the dependency graph; any module can include it without pulling in other GameCore systems.
- **Tag-based identity** — uses `FGameplayTag` for structured, hierarchical identification (`Source.Mob.Skeleton.Level10`, `Source.Quest.MainStory.Act1`, `Source.Market.PlayerTrade`). Tags are editor-validated and zero-cost to compare.
- **Optional display name** — `GetSourceDisplayName()` is non-pure with an empty default, so implementors only override it when CS/debugging tooling is needed.
- **Server-relevant only** — authoritative grant paths live on the server. The client never needs to evaluate source identity.
- **Stateless implementations preferred** — `GetSourceTag()` should derive the tag from existing object data, not store a redundant field.
- **Not an event bus** — this interface is identification only, not a communication mechanism.

---

## Gameplay Tag Hierarchy Convention

| Prefix | Used For |
|---|---|
| `Source.Mob.*` | Enemy actors, NPCs |
| `Source.Quest.*` | Quest and objective completions |
| `Source.Event.*` | World events, seasonal events |
| `Source.Market.*` | Player economy transactions |
| `Source.Admin.*` | GM/CS manual grants |
| `Source.System.*` | Automated system grants (login bonus, etc.) |

These tags must be registered in a GameCore Core tags `.ini` or via `AddNativeGameplayTag` in `StartupModule`.

---

## Class Definition

```cpp
// SourceIDInterface.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "SourceIDInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class USourceIDInterface : public UInterface
{
    GENERATED_BODY()
};

/**
 * Implemented by any UObject that can identify itself as an event source.
 * Used by logging, audit trails, and analytics across multiple systems
 * (XP grants, item drops, market events, etc.).
 *
 * Tag convention: Source.<Category>.<SubType>.<Detail>
 * Examples:
 *   Source.Mob.Skeleton.Level10
 *   Source.Quest.MainStory.Act1
 *   Source.Market.PlayerTrade
 *   Source.Event.SeasonalEvent.WinterFest
 */
class GAMECORE_API ISourceIDInterface
{
    GENERATED_BODY()

public:
    /**
     * Returns a structured gameplay tag identifying this source.
     * Must be overridden. Tag should follow the Source.* hierarchy.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Source ID")
    FGameplayTag GetSourceTag() const;
    virtual FGameplayTag GetSourceTag_Implementation() const = 0;

    /**
     * Optional human-readable name for CS tooling and debug logs.
     * Returns empty text by default; override only when tooling requires it.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Source ID")
    FText GetSourceDisplayName() const;
    virtual FText GetSourceDisplayName_Implementation() const
    {
        return FText::GetEmpty();
    }
};
```
