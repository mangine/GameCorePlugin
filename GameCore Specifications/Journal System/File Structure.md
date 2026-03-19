# File Structure

**Sub-page of:** [Journal System](../Journal%20System.md)

---

## Module Layout

```
GameCore/
└── Source/
    └── GameCore/
        └── Journal/
            ├── JournalTypes.h                    -- FJournalEntryHandle, FJournalRenderedDetails,
            │                                        FJournalCollectionProgress, GMS message structs,
            │                                        delegate declarations
            ├── JournalEntry.h / .cpp             -- UJournalEntry (UInterface), IJournalEntry
            ├── JournalEntryDataAsset.h / .cpp    -- UJournalEntryDataAsset (Abstract base)
            ├── JournalCollectionDefinition.h / .cpp -- UJournalCollectionDefinition
            ├── JournalComponent.h / .cpp         -- UJournalComponent
            └── JournalRegistrySubsystem.h / .cpp -- UJournalRegistrySubsystem
```

Concrete `UJournalEntryDataAsset` subclasses (e.g. `UBookJournalEntryDataAsset`, `UQuestJournalEntryDataAsset`) live in the **game module**, not in GameCore.

---

## Build.cs Dependencies

```csharp
// GameCore.Build.cs — no new dependencies required.
// Journal system uses only:
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core",
    "CoreUObject",
    "Engine",
    "GameplayTags",
    "NetCore",         // For fast TArray replication if needed
});
```

Game module `Build.cs` — for concrete entry asset subclasses:
```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameCore",
    "GameplayTags",
});
```

---

## Gameplay Tags

Add the following to `DefaultGameplayTags.ini` in GameCore:

```ini
[/Script/GameplayTags.GameplayTagsSettings]
+GameplayTagList=(Tag="GameCoreEvent.Journal.EntryAdded",DevComment="Fired server-side when a journal entry is added")
+GameplayTagList=(Tag="Journal.Track",DevComment="Namespace for track tags — leaf tags defined in game module")
```

Game module adds its own track, entry, and collection tags:

```ini
+GameplayTagList=(Tag="Journal.Track.Books",DevComment="Lore books and scrolls track")
+GameplayTagList=(Tag="Journal.Track.Adventure",DevComment="Quests, events, places track")
+GameplayTagList=(Tag="Journal.Entry",DevComment="Namespace for all entry identity tags")
+GameplayTagList=(Tag="Journal.Collection",DevComment="Namespace for all collection tags")
```

---

## Delegate Declarations

```cpp
// JournalTypes.h
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnJournalSynced);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnJournalEntryAdded, FJournalEntryHandle, NewHandle);
```
