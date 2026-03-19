# GMS Messages & Channel Tags

All Zone System events are broadcast via `UGameplayMessageSubsystem`. No delegates are declared on zone actors or components.

---

## Channel Tags

Declare these in the project's `GameplayTags` ini or in a `UGameplayTagsManager` native call within the GameCore module startup.

```
GameCore.Zone.Channel.Transition     — enter/exit events from UZoneTrackerComponent
GameCore.Zone.Channel.StateChanged   — FZoneDynamicState changed on a zone actor (OnRep)
```

In C++, expose as constants:

```cpp
// ZoneChannelTags.h  (GameCore module)
namespace GameCore::Zone::Tags
{
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_Transition)
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_StateChanged)
}

// ZoneChannelTags.cpp
namespace GameCore::Zone::Tags
{
    UE_DEFINE_GAMEPLAY_TAG(Channel_Transition,   "GameCore.Zone.Channel.Transition")
    UE_DEFINE_GAMEPLAY_TAG(Channel_StateChanged, "GameCore.Zone.Channel.StateChanged")
}
```

---

## `FZoneTransitionMessage`

Broadcast on `GameCore.Zone.Channel.Transition` by `UZoneTrackerComponent`.

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

    // Snapshot of static data at time of transition.
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UZoneDataAsset> StaticData;

    // Snapshot of dynamic state at time of transition.
    UPROPERTY(BlueprintReadOnly)
    FZoneDynamicState DynamicState;

    // True = entered, False = exited.
    UPROPERTY(BlueprintReadOnly)
    bool bEntered = false;
};
```

---

## `FZoneStateChangedMessage`

Broadcast on `GameCore.Zone.Channel.StateChanged` by `AZoneActor::OnRep_DynamicState` (clients) and by the server-side setter methods.

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

> **Server broadcast:** When the server calls `SetOwnerTag` or `AddDynamicTag`, it should also broadcast `FZoneStateChangedMessage` immediately (so server-side listeners don't wait for the rep notify).

```cpp
void AZoneActor::SetOwnerTag(FGameplayTag NewOwner)
{
    DynamicState.OwnerTag = NewOwner;

    FZoneStateChangedMessage Msg;
    Msg.ZoneActor    = this;
    Msg.StaticData   = DataAsset;
    Msg.DynamicState = DynamicState;
    UGameplayMessageSubsystem::Get(this).BroadcastMessage(
        GameCore::Zone::Tags::Channel_StateChanged, Msg);
}
```

---

## Listener Registration Examples

### React to zone enter/exit (any system)

```cpp
ListenerHandle = GMS.RegisterListener<FZoneTransitionMessage>(
    GameCore::Zone::Tags::Channel_Transition,
    this,
    &UMyWeatherSystem::OnZoneTransition
);

void UMyWeatherSystem::OnZoneTransition(
    FGameplayTag Channel, const FZoneTransitionMessage& Msg)
{
    if (!Msg.StaticData) return;

    if (Msg.StaticData->ZoneTypeTag.MatchesTag(MyTags::ZoneType_Weather))
    {
        if (Msg.bEntered) ApplyWeatherPreset(Msg.StaticData);
        else              ClearWeatherPreset();
    }
}
```

### React to ownership change

```cpp
ListenerHandle = GMS.RegisterListener<FZoneStateChangedMessage>(
    GameCore::Zone::Tags::Channel_StateChanged,
    this,
    &UMyTerritoryHUD::OnZoneStateChanged
);

void UMyTerritoryHUD::OnZoneStateChanged(
    FGameplayTag Channel, const FZoneStateChangedMessage& Msg)
{
    if (Msg.StaticData->ZoneTypeTag.MatchesTag(MyTags::ZoneType_Territory))
        RefreshTerritoryUI(Msg.ZoneActor, Msg.DynamicState.OwnerTag);
}
```

> **Important:** Always unregister the listener handle in `EndPlay` or the owning object's destructor to avoid dangling callbacks.
