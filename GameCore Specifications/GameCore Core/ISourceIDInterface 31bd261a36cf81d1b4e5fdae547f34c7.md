# ISourceIDInterface

## Overview

`ISourceIDInterface` is a **generic GameCore interface** that any UObject can implement to declare itself as an identifiable source. It is intentionally broad — it is not tied to XP, drops, or any specific system. Any system that needs to record *where something came from* (XP grants, item drops, market transactions, quest rewards, etc.) can accept a `TScriptInterface<ISourceIDInterface>` and log a structured identity without coupling to a concrete class.

## Plugin Module

`GameCore` (runtime module)

## File Location

```
GameCore/
└── Source/
    └── GameCore/
        └── Core/
            └── SourceID/
                └── SourceIDInterface.h
```

## Design Rationale

- **No system dependency** — lives at the bottom of the dependency graph; any module can include it.
- **Tag-based identity** — uses `FGameplayTag` for structured, hierarchical identification (e.g. `Source.Mob.Skeleton.Level10`, `Source.Quest.MainStory.Act1`, `Source.Market.PlayerTrade`).
- **Optional display name** — `GetSourceDisplayName()` is non-pure with an empty default, so implementors only override it when CS/debugging tooling is needed.
- **Reusable across systems** — XP audit trails, drop logs, market logs, and any future system all share the same contract.

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
     * Returns empty text by default.
     */
    UFUNCTION(BlueprintNativeEvent, Category = "Source ID")
    FText GetSourceDisplayName() const;
    virtual FText GetSourceDisplayName_Implementation() const
    {
        return FText::GetEmpty();
    }
};
```

## Usage Example

Any system that accepts a source simply takes a `TScriptInterface`:

```cpp
// In any system (XP, drops, market...)
void AddXP(
    FGameplayTag ProgressionTag,
    int32 Amount,
    TScriptInterface<ISourceIDInterface> Source = nullptr
);

// Logging side
if (Source.GetObject())
{
    FGameplayTag SourceTag  = Source->GetSourceTag();
    FText        SourceName = Source->GetSourceDisplayName();
    // Forward to backend telemetry, audit log, etc.
}
```

## Gameplay Tag Hierarchy Convention

| Prefix | Used For |
| --- | --- |
| `Source.Mob.*` | Enemy actors, NPCs |
| `Source.Quest.*` | Quest and objective completions |
| `Source.Event.*` | World events, seasonal events |
| `Source.Market.*` | Player economy transactions |
| `Source.Admin.*` | GM/CS manual grants |
| `Source.System.*` | Automated system grants (login bonus, etc.) |

## Notes

- This interface is **server-relevant only** in the context of authoritative grants. The client never needs to evaluate source identity.
- Implementations should be **stateless** — `GetSourceTag()` should derive the tag from the object's existing data, not store a separate field unless necessary.
- Do **not** use this interface as a general event bus — it is identification only.