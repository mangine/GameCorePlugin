# Alignment Event Tags

Channel tags and native tag handle declarations for the Alignment System. Follows the same pattern as all other GameCore event channels.

---

## `DefaultGameplayTags.ini` — GameCore Module

Add to `GameCore/Config/DefaultGameplayTags.ini`:

```ini
[/Script/GameplayTags.GameplayTagsList]
+GameplayTagList=(Tag="GameCoreEvent.Alignment.Changed")
```

---

## `GameCoreEventTags.h` — Additions

```cpp
namespace GameCoreEventTags
{
    // ... existing tags ...

    /** Fired once per ApplyAlignmentDeltas call when at least one axis changed. */
    GAMECORE_API extern FGameplayTag Alignment_Changed;
}
```

---

## `GameCoreEventTags.cpp` — Additions

```cpp
namespace GameCoreEventTags
{
    // ... existing tags ...
    FGameplayTag Alignment_Changed;
}

// In FGameCoreModule::StartupModule():
GameCoreEventTags::Alignment_Changed =
    UGameplayTagsManager::Get().AddNativeGameplayTag(
        TEXT("GameCoreEvent.Alignment.Changed"),
        TEXT("Fired once per UAlignmentComponent::ApplyAlignmentDeltas call when at least one axis changed."));
```

---

## Channel Reference

| Tag | Struct | Scope | Origin | Client reaction |
|---|---|---|---|---|
| `GameCoreEvent.Alignment.Changed` | `FAlignmentChangedMessage` | `ServerOnly` | `UAlignmentComponent::ApplyAlignmentDeltas` | `FFastArraySerializer` replication → `OnAlignmentDataReplicated` delegate |

---

## Axis Tags (Game Module)

Axis tags are **not** defined in `GameCore`. They belong in the consuming game module's `DefaultGameplayTags.ini`:

```ini
; MyGame/Config/DefaultGameplayTags.ini
[/Script/GameplayTags.GameplayTagsList]
+GameplayTagList=(Tag="Alignment.GoodEvil")
+GameplayTagList=(Tag="Alignment.LawChaos")
+GameplayTagList=(Tag="Alignment.Honor")
```

The GameCore plugin has zero awareness of specific axis names.
