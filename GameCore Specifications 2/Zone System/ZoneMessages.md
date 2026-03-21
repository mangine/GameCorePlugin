# Zone Messages & Channel Tags

**File:** `GameCore/Source/GameCore/Zone/ZoneMessages.h` and `ZoneChannelTags.h` / `.cpp`

All Zone System events are broadcast via `UGameCoreEventBus`. No delegates are declared on zone actors or components.

---

## Channel Tags

```cpp
// ZoneChannelTags.h  (GameCore module)
namespace GameCore::Zone::Tags
{
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_Transition)    // enter/exit events from UZoneTrackerComponent
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_StateChanged)  // FZoneDynamicState changed on a zone actor
}

// ZoneChannelTags.cpp
namespace GameCore::Zone::Tags
{
    UE_DEFINE_GAMEPLAY_TAG(Channel_Transition,   "GameCore.Zone.Channel.Transition")
    UE_DEFINE_GAMEPLAY_TAG(Channel_StateChanged, "GameCore.Zone.Channel.StateChanged")
}
```

Register these tags in the project's `GameplayTags` ini or via `UGameplayTagsManager` in the GameCore module startup:

```
GameCore.Zone.Channel.Transition
GameCore.Zone.Channel.StateChanged
```

---

## `FZoneTransitionMessage`

Broadcast on `GameCore.Zone.Channel.Transition` by `UZoneTrackerComponent::BroadcastTransition`.

```cpp
USTRUCT(BlueprintType)
struct FZoneTransitionMessage
{
    GENERATED_BODY()

    // The actor that crossed the zone boundary.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AActor> TrackedActor;

    // The zone being entered or exited.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AZoneActor> ZoneActor;

    // Snapshot of static data at the time of transition.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UZoneDataAsset> StaticData;

    // Snapshot of dynamic state at the time of transition.
    UPROPERTY(BlueprintReadOnly)
    FZoneDynamicState DynamicState;

    // True = entered, False = exited.
    UPROPERTY(BlueprintReadOnly)
    bool bEntered = false;
};
```

---

## `FZoneStateChangedMessage`

Broadcast on `GameCore.Zone.Channel.StateChanged` by:
- **Server:** `AZoneActor::BroadcastStateChanged()` — called from `SetOwnerTag`, `AddDynamicTag`, `RemoveDynamicTag` (scope: `ServerOnly`)
- **Clients:** `AZoneActor::OnRep_DynamicState()` (scope: `ClientOnly`)

```cpp
USTRUCT(BlueprintType)
struct FZoneStateChangedMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AZoneActor> ZoneActor;

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UZoneDataAsset> StaticData;

    // The new dynamic state.
    UPROPERTY(BlueprintReadOnly)
    FZoneDynamicState DynamicState;
};
```

---

## Listener Registration Pattern

Always store the handle as a member and call `StopListening` in `EndPlay`. Leaked handles keep a dangling lambda alive in GMS indefinitely.

```cpp
// MySystem.h
private:
    FGameplayMessageListenerHandle TransitionHandle;
    FGameplayMessageListenerHandle StateHandle;

// MySystem.cpp
void UMySystem::BeginPlay()
{
    Super::BeginPlay();
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        TransitionHandle = Bus->StartListening<FZoneTransitionMessage>(
            GameCore::Zone::Tags::Channel_Transition,
            [this](FGameplayTag, const FZoneTransitionMessage& Msg) { /* ... */ });

        StateHandle = Bus->StartListening<FZoneStateChangedMessage>(
            GameCore::Zone::Tags::Channel_StateChanged,
            [this](FGameplayTag, const FZoneStateChangedMessage& Msg) { /* ... */ });
    }
}

void UMySystem::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        Bus->StopListening(TransitionHandle);
        Bus->StopListening(StateHandle);
    }
    Super::EndPlay(Reason);
}
```
