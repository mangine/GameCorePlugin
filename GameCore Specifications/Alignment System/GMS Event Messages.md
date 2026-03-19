# GMS Event Messages

**Sub-page of:** [Alignment System](../Alignment%20System.md)

All Alignment System GMS messages are defined in `GameCoreEventMessages.h` alongside the other GameCore event structs. Channel tags are registered in `DefaultGameplayTags.ini` and cached as native tag handles at module startup.

---

## File Location

```
GameCore/Source/GameCore/EventBus/GameCoreEventMessages.h
```

---

## Channel Tag Definitions

Add to `DefaultGameplayTags.ini` inside the `GameCore` module:

```ini
[/Script/GameplayTags.GameplayTagsList]
+GameplayTagList=(Tag="GameCoreEvent.Alignment.Changed")
```

Native tag handle cached at module startup in `GameCoreEventTags.h`:

```cpp
namespace GameCoreEventTags
{
    // ... existing tags ...
    GAMECORE_API extern FGameplayTag Alignment_Changed;
}
```

Registered via `UGameplayTagsManager::AddNativeGameplayTag` in `StartupModule`. Zero-cost lookup at broadcast sites.

---

## `FAlignmentChangedEntry` — Per-Axis Change Record

```cpp
// One axis record within a batch alignment change broadcast.
// Only axes that actually changed (non-zero AppliedDelta) appear in the array.
USTRUCT(BlueprintType)
struct GAMECORE_API FAlignmentChangedEntry
{
    GENERATED_BODY()

    // The axis that changed.
    UPROPERTY() FGameplayTag AlignmentTag;

    // Actual delta applied after saturation clamping.
    // May differ from the requested delta if the underlying value hit SaturatedMin/Max.
    // Never zero — entries with zero applied delta are excluded from the broadcast.
    UPROPERTY() float AppliedDelta  = 0.f;

    // New underlying value after mutation.
    UPROPERTY() float NewUnderlying = 0.f;

    // New effective value after mutation = Clamp(NewUnderlying, EffectiveMin, EffectiveMax).
    // Pre-computed server-side so listeners do not need the definition asset.
    UPROPERTY() float NewEffective  = 0.f;
};
```

---

## `FAlignmentChangedMessage`

**Channel:** `GameCoreEvent.Alignment.Changed`
**Scope:** `ServerOnly`
**Origin:** `UAlignmentComponent::ApplyAlignmentDeltas` — server only.
**Client reaction:** Clients observe alignment changes via `FFastArraySerializer` replication of `FAlignmentArray`. No client-side GMS broadcast is fired.

```cpp
// Broadcast once per ApplyAlignmentDeltas call.
// Contains all axes that changed in that batch.
// Axes that were skipped (requirements failed, zero delta, already saturated) are excluded.
USTRUCT(BlueprintType)
struct GAMECORE_API FAlignmentChangedMessage
{
    GENERATED_BODY()

    // The player whose alignment changed.
    UPROPERTY() TObjectPtr<APlayerState> PlayerState = nullptr;

    // All axes that actually changed in this batch. Never empty when broadcast fires.
    // Guaranteed: at least one entry exists.
    UPROPERTY() TArray<FAlignmentChangedEntry> Changes;
};
```

---

## Listening — Integration Pattern

```cpp
// In any server-side system that reacts to alignment changes:

void UMySystem::BeginPlay()
{
    Super::BeginPlay();

    if (UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this))
    {
        AlignmentHandle = Bus->StartListening<FAlignmentChangedMessage>(
            GameCoreEventTags::Alignment_Changed,
            this,
            &UMySystem::OnAlignmentChanged);
    }
}

void UMySystem::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UGameCoreEventSubsystem* Bus = UGameCoreEventSubsystem::Get(this))
    {
        Bus->StopListening(AlignmentHandle);
    }
    Super::EndPlay(Reason);
}

void UMySystem::OnAlignmentChanged(FGameplayTag Channel, const FAlignmentChangedMessage& Msg)
{
    // Early-out if this is not the player we care about.
    if (Msg.PlayerState != MyTrackedPlayerState) return;

    for (const FAlignmentChangedEntry& Entry : Msg.Changes)
    {
        if (Entry.AlignmentTag == AlignmentTags::GoodEvil)
        {
            // React: Entry.NewEffective is ready to use — no definition asset needed.
            HandleGoodEvilChanged(Msg.PlayerState, Entry.NewEffective, Entry.AppliedDelta);
        }
    }
}
```

> **Always store the handle and call `StopListening` in `EndPlay`.** Leaked handles keep a dangling reference inside GMS.

---

## Authoring Rules for New Alignment Messages

If a future requirement adds a second alignment event (e.g. `Alignment.AxisRegistered` for tracking when a new axis is first enabled):

1. Define a new `USTRUCT` in `GameCoreEventMessages.h`.
2. Add a new tag under `GameCoreEvent.Alignment.*` in `DefaultGameplayTags.ini`.
3. Add a native tag handle to the `GameCoreEventTags` namespace.
4. Declare **Scope** and **Origin machine** in the channel documentation.
5. Document the client reaction path (since scope will typically be `ServerOnly`).

---

## Why `NewEffective` Is Pre-Computed

Listeners commonly need the effective value (for UI, quest checks, etc.) but should not be required to hold a reference to `UAlignmentDefinition` assets just to perform the clamp. Pre-computing `NewEffective` server-side in `ApplyAlignmentDeltas` keeps listeners simple and definition-asset-free.

The cost is two floats per changed axis per broadcast — negligible.
